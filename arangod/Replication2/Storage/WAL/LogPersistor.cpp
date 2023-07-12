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
/// @author Manuel Pöter
////////////////////////////////////////////////////////////////////////////////

#include "LogPersistor.h"

#include <absl/crc/crc32c.h>
#include <type_traits>

#include "Assertions/Assert.h"
#include "Basics/Result.h"
#include "Basics/ResultT.h"
#include "Basics/voc-errors.h"
#include "Futures/Future.h"
#include "Replication2/Storage/IteratorPosition.h"
#include "Replication2/Storage/WAL/Buffer.h"
#include "Replication2/Storage/WAL/Entry.h"
#include "Replication2/Storage/WAL/EntryType.h"
#include "Replication2/Storage/WAL/FileIterator.h"
#include "Replication2/Storage/WAL/IFileReader.h"
#include "Replication2/Storage/WAL/ILogFile.h"
#include "Replication2/Storage/WAL/IWalManager.h"
#include "velocypack/Builder.h"

namespace arangodb::replication2::storage::wal {

LogPersistor::LogPersistor(LogId logId, std::shared_ptr<IWalManager> walManager)
    : _logId(logId),
      _walManager(std::move(walManager)),
      _file(_walManager->openLog(_logId)) {
  ;
}

auto LogPersistor::getIterator(IteratorPosition position)
    -> std::unique_ptr<PersistedLogIterator> {
  return std::make_unique<FileIterator>(position, _file->getReader());
}

auto LogPersistor::insert(std::unique_ptr<LogIterator> iter,
                          WriteOptions const& writeOptions)
    -> futures::Future<ResultT<SequenceNumber>> {
  Buffer buffer;
  std::uint64_t const zero = 0;

  SequenceNumber seq = 0;  // TODO - what if iterator is empty?
  while (auto e = iter->next()) {
    buffer.clear();

    // TODO - clean up a bit
    auto& entry = e.value();
    seq = entry.logIndex().value;

    Entry::Header header;
    header.index = entry.logIndex().value;
    header.term = entry.logTerm().value;

    // we want every thing to be 8 byte aligned, so we round size to the next
    // greater multiple of 8
    if (entry.hasPayload()) {
      header.type = EntryType::wNormal;
      auto* payload = entry.logPayload();
      TRI_ASSERT(payload != nullptr);
      header.size = payload->byteSize();
      buffer.append(header.compress());
      buffer.append(payload->slice().getDataPtr(), payload->byteSize());
    } else {
      TRI_ASSERT(entry.hasMeta());
      header.type = EntryType::wMeta;
      // the meta entry is directly encoded into the buffer, so we have to write
      // the header beforehand and update the size afterwards
      buffer.append(header.compress());
      auto pos = buffer.size() - sizeof(Entry::CompressedHeader::size);

      auto* payload = entry.meta();
      TRI_ASSERT(payload != nullptr);
      VPackBuilder builder(buffer.buffer());
      payload->toVelocyPack(builder);
      header.size = buffer.size() - pos - sizeof(Entry::CompressedHeader::size);

      // reset to the saved position so we can write the actual size
      buffer.resetTo(pos);
      buffer.append(
          static_cast<decltype(Entry::CompressedHeader::size)>(header.size));
      buffer.advance(header.size);
    }

    // write zeroed out padding bytes
    auto nunPaddingBytes =
        Entry::paddedPayloadSize(buffer.size()) - buffer.size();
    TRI_ASSERT(nunPaddingBytes < 8);
    buffer.append(&zero, nunPaddingBytes);

    Entry::Footer footer{
        .crc32 = static_cast<std::uint32_t>(absl::ComputeCrc32c(
            {reinterpret_cast<char const*>(buffer.data()), buffer.size()})),
        .size =
            static_cast<std::uint32_t>(buffer.size() + sizeof(Entry::Footer))};
    TRI_ASSERT(footer.size % 8 == 0);
    buffer.append(footer);
    TRI_ASSERT(buffer.size() % 8 == 0);
    _file->append(
        {reinterpret_cast<char const*>(buffer.data()), buffer.size()});

    if (writeOptions.waitForSync) {
      _file->sync();
    }
  }

  return ResultT<SequenceNumber>::success(seq);
}

auto LogPersistor::removeFront(LogIndex stop, WriteOptions const&)
    -> futures::Future<ResultT<SequenceNumber>> {
  // TODO
  return ResultT<SequenceNumber>::error(TRI_ERROR_NOT_IMPLEMENTED);
}

auto LogPersistor::removeBack(LogIndex start, WriteOptions const&)
    -> futures::Future<ResultT<SequenceNumber>> {
  // TODO
  return ResultT<SequenceNumber>::error(TRI_ERROR_NOT_IMPLEMENTED);
}

auto LogPersistor::getLogId() -> LogId { return _logId; }

auto LogPersistor::waitForSync(SequenceNumber) -> futures::Future<Result> {
  // TODO
  return Result(TRI_ERROR_NOT_IMPLEMENTED);
}

// waits for all ongoing requests to be done
void LogPersistor::waitForCompletion() noexcept {
  // TODO
}

auto LogPersistor::compact() -> Result {
  // nothing to do here - this function should be removed from the interface
  return Result{};
}

auto LogPersistor::drop() -> Result { return _walManager->dropLog(_logId); }

}  // namespace arangodb::replication2::storage::wal
