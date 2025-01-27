////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2014-2023 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Alexandru Petenchea
////////////////////////////////////////////////////////////////////////////////

#include "Replication2/StateMachines/Document/DocumentStateShardHandler.h"

#include "Replication2/StateMachines/Document/MaintenanceActionExecutor.h"
#include "VocBase/vocbase.h"
#include "VocBase/LogicalCollection.h"
#include "VocBase/VocBaseLogManager.h"
#include "Transaction/Methods.h"
#include "Transaction/StandaloneContext.h"
#ifdef USE_V8
#include "Transaction/V8Context.h"
#endif
#include "Utils/SingleCollectionTransaction.h"

namespace arangodb::replication2::replicated_state::document {

DocumentStateShardHandler::DocumentStateShardHandler(
    TRI_vocbase_t& vocbase, GlobalLogIdentifier gid,
    std::shared_ptr<IMaintenanceActionExecutor> maintenance)
    : _gid(std::move(gid)),
      _maintenance(std::move(maintenance)),
      _vocbase(vocbase) {
  auto manager = vocbase._logManager;
  // TODO this is more like a hack than an actual solution
  //  but for now its good enough

  auto [from, to] = manager->_initCollections.equal_range(_gid.id);
  for (auto iter = from; iter != to; ++iter) {
    auto const& coll = iter->second;
    auto properties = std::make_shared<VPackBuilder>();
    coll->properties(*properties, LogicalDataSource::Serialization::Properties);
    _shardMap.shards.emplace(
        coll->name(),
        ShardProperties{std::to_string(coll->planId().id()), properties});
  }
  manager->_initCollections.erase(from, to);
}

auto DocumentStateShardHandler::ensureShard(
    ShardID shard, CollectionID collection,
    std::shared_ptr<VPackBuilder> properties) -> ResultT<bool> {
  std::unique_lock lock(_shardMap.mutex);
  if (_shardMap.shards.contains(shard)) {
    return false;
  }

  if (auto res = _maintenance->executeCreateCollectionAction(shard, collection,
                                                             properties);
      res.fail()) {
    return res;
  }

  _shardMap.shards.emplace(
      std::move(shard),
      ShardProperties{std::move(collection), std::move(properties)});
  lock.unlock();

  _maintenance->addDirty();
  return true;
}

auto DocumentStateShardHandler::modifyShard(ShardID shard,
                                            CollectionID collection,
                                            velocypack::SharedSlice properties)
    -> Result {
  // The shard map is not updated with the new properties.
  // We are in progress of reworking the snapshot transfer to accomodate such
  // changes. I am not even sure whether this map is still needed at all, we'll
  // figure it out in an upcoming PR.
  std::shared_lock lock(_shardMap.mutex);
  if (!_shardMap.shards.contains(shard)) {
    LOG_TOPIC("5de59", TRACE, arangodb::Logger::REPLICATED_STATE)
        << "Shard " << shard << " not found in database " << _vocbase.name()
        << " while trying to modifying it";

    // Don't fail if the shard is not found.
    return {};
  }

  if (auto res = _maintenance->executeModifyCollectionAction(
          std::move(shard), std::move(collection), std::move(properties));
      res.fail()) {
    return res;
  }
  lock.unlock();

  _maintenance->addDirty();
  return {};
}

auto DocumentStateShardHandler::dropShard(ShardID const& shard)
    -> ResultT<bool> {
  std::unique_lock lock(_shardMap.mutex);
  auto it = _shardMap.shards.find(shard);
  if (it == std::end(_shardMap.shards)) {
    return false;
  }

  if (auto res = _maintenance->executeDropCollectionAction(
          shard, it->second.collection);
      res.fail()) {
    return res;
  }
  _shardMap.shards.erase(shard);
  lock.unlock();

  _maintenance->addDirty();
  return true;
}

auto DocumentStateShardHandler::dropAllShards() -> Result {
  std::unique_lock lock(_shardMap.mutex);
  for (auto const& [shard, properties] : _shardMap.shards) {
    if (auto res = _maintenance->executeDropCollectionAction(
            shard, properties.collection);
        res.fail()) {
      return Result{res.errorNumber(),
                    fmt::format("Failed to drop shard {}: {}", shard,
                                res.errorMessage())};
    }
  }
  _shardMap.shards.clear();
  lock.unlock();

  _maintenance->addDirty();
  return {};
}

auto DocumentStateShardHandler::isShardAvailable(const ShardID& shard) -> bool {
  std::shared_lock lock(_shardMap.mutex);
  return _shardMap.shards.contains(shard);
}

auto DocumentStateShardHandler::getShardMap() -> ShardMap {
  std::shared_lock lock(_shardMap.mutex);
  return _shardMap.shards;
}

auto DocumentStateShardHandler::ensureIndex(
    ShardID shard, std::shared_ptr<VPackBuilder> const& properties,
    std::shared_ptr<methods::Indexes::ProgressTracker> progress) -> Result {
  auto res = basics::catchToResult(
      [this, &properties, shard = shard,
       progress = std::move(progress)]() mutable -> Result {
        return _maintenance->executeCreateIndex(shard, properties,
                                                std::move(progress));
      });

  if (res.ok()) {
    _maintenance->addDirty();
  } else {
    res = Result{res.errorNumber(),
                 fmt::format("Error: {}! Failed to ensure index on shard {}! "
                             "Index: {}",
                             res.errorMessage(), std::move(shard),
                             properties->toJson())};
  }
  return res;
}

auto DocumentStateShardHandler::dropIndex(ShardID shard,
                                          velocypack::SharedSlice index)
    -> Result {
  auto indexId = index.toString();
  auto res = basics::catchToResult(
      [this, shard, index = std::move(index)]() mutable -> Result {
        return _maintenance->executeDropIndex(std::move(shard),
                                              std::move(index));
      });

  if (res.ok()) {
    _maintenance->addDirty();
  } else {
    res = Result{
        res.errorNumber(),
        fmt::format("Error {}! Failed to drop index on shard {}! Index: {}",
                    res.errorMessage(), std::move(shard), std::move(indexId))};
  }
  return res;
}

auto DocumentStateShardHandler::lockShard(ShardID const& shard,
                                          AccessMode::Type accessType,
                                          transaction::OperationOrigin origin)
    -> ResultT<std::unique_ptr<transaction::Methods>> {
  auto col = _vocbase.lookupCollection(shard);
  if (col == nullptr) {
    return Result{TRI_ERROR_ARANGO_DATA_SOURCE_NOT_FOUND,
                  fmt::format("Failed to lookup shard {} in database {} while "
                              "locking it for operation {}",
                              shard, _vocbase.name(), origin.description)};
  }

#ifdef USE_V8
  auto ctx =
      transaction::V8Context::createWhenRequired(_vocbase, origin, false);
#else
  auto ctx = transaction::StandaloneContext::create(_vocbase, origin);
#endif

  // Not replicating this transaction
  transaction::Options options;
  options.requiresReplication = false;

  auto trx = std::make_unique<SingleCollectionTransaction>(std::move(ctx), *col,
                                                           accessType, options);
  Result res = trx->begin();
  if (res.fail()) {
    return Result{res.errorNumber(),
                  fmt::format("Failed to lock shard {} in database "
                              "{} for operation {}. Error: {}",
                              shard, _vocbase.name(), origin.description,
                              res.errorMessage())};
  }
  return trx;
}

}  // namespace arangodb::replication2::replicated_state::document
