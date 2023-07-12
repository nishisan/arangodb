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

#include <gmock/gmock.h>

#include "Basics/ResultT.h"
#include "Futures/Future.h"
#include "Replication2/ReplicatedLog/InMemoryLog.h"
#include "Replication2/ReplicatedLog/InMemoryLogEntry.h"
#include "Replication2/ReplicatedLog/LogMetaPayload.h"
#include "Replication2/Storage/WAL/Entry.h"
#include "Replication2/Storage/WAL/LogPersistor.h"
#include "Replication2/Storage/WAL/StreamReader.h"

#include "Replication2/Storage/InMemoryLogFile.h"
#include "Replication2/Storage/MockWalManager.h"
#include "velocypack/Builder.h"
#include "velocypack/Slice.h"

#include <absl/crc/crc32c.h>
#include <cstdint>
#include <cstring>
#include <immer/flex_vector_transient.hpp>
#include <variant>

using namespace ::testing;
using namespace arangodb::replication2;
using namespace arangodb::replication2::replicated_log;

namespace arangodb::replication2::storage::wal::test {

struct LogPersistorTest : ::testing::Test {
  void SetUp() override {
    auto file = std::make_unique<InMemoryLogFile>(buffer);
    EXPECT_CALL(*walManager, openLog(_)).WillOnce(Return(std::move(file)));
  }

  void checkLogEntry(StreamReader& reader, LogIndex idx, LogTerm term,
                     EntryType type,
                     std::variant<LogPayload, LogMetaPayload> payload) {
    auto* data = reader.data();
    auto dataSize = reader.size();

    VPackBuilder builder;
    VPackSlice payloadSlice;
    if (std::holds_alternative<LogPayload>(payload)) {
      auto const& p = get<LogPayload>(payload);
      payloadSlice = p.slice();
    } else {
      auto const& p = get<LogMetaPayload>(payload);
      p.toVelocyPack(builder);
      payloadSlice = builder.slice();
    }

    auto expectedSize =
        sizeof(Entry::CompressedHeader) +
        Entry::paddedPayloadSize(payloadSlice.byteSize())  // payload
        + sizeof(Entry::Footer);
    ASSERT_EQ(dataSize, expectedSize);

    auto compressedHeader = reader.read<Entry::CompressedHeader>();
    auto header = Entry::Header::fromCompressed(compressedHeader);

    EXPECT_EQ(idx.value, header.index) << "Log index mismatch";
    EXPECT_EQ(term.value, header.term) << "Log term mismatch";
    EXPECT_EQ(type, header.type) << "Entry type mismatch";
    EXPECT_EQ(payloadSlice.byteSize(), header.size) << "size mismatch";

    EXPECT_TRUE(memcmp(reader.data(), payloadSlice.getDataPtr(),
                       payloadSlice.byteSize()) == 0)
        << "Payload mismatch";

    auto paddedSize = Entry::paddedPayloadSize(header.size);
    reader.skip(paddedSize);

    auto footer = reader.read<Entry::Footer>();

    auto expectedCrc = static_cast<std::uint32_t>(
        absl::ComputeCrc32c({reinterpret_cast<char const*>(data),
                             sizeof(Entry::CompressedHeader) + paddedSize}));
    EXPECT_EQ(expectedCrc, footer.crc32);
    EXPECT_EQ(expectedSize, footer.size);
  }

  std::string buffer;
  std::shared_ptr<MockWalManager> walManager{
      std::make_shared<MockWalManager>()};
};

TEST_F(LogPersistorTest, drop_calls_wal_manager_drop) {
  EXPECT_CALL(*walManager, dropLog(LogId{42})).Times(1);
  LogPersistor persistor{LogId{42}, walManager};
  persistor.drop();
  Mock::VerifyAndClearExpectations(walManager.get());
}

TEST_F(LogPersistorTest, insert_normal_payload) {
  LogPersistor persistor{LogId{42}, walManager};

  auto payload = LogPayload::createFromString("foobar");
  auto log = InMemoryLog{}.append(
      {InMemoryLogEntry{LogEntry{LogTerm{1}, LogIndex{100}, payload}}});

  auto res =
      persistor.insert(log.getLogIterator(), LogPersistor::WriteOptions{})
          .get();
  ASSERT_TRUE(res.ok());
  EXPECT_EQ(res.get(), 100);

  StreamReader reader{buffer.data(), buffer.size()};
  checkLogEntry(reader, LogIndex{100}, LogTerm{1}, EntryType::wNormal, payload);
}

TEST_F(LogPersistorTest, insert_meta_payload) {
  LogPersistor persistor{LogId{42}, walManager};

  LogMetaPayload::Ping::clock::time_point tp{};
  auto payload = LogMetaPayload::withPing("message", tp);
  auto log = InMemoryLog{}.append(
      {InMemoryLogEntry{LogEntry{LogTerm{1}, LogIndex{100}, payload}}});

  auto res =
      persistor.insert(log.getLogIterator(), LogPersistor::WriteOptions{})
          .get();
  ASSERT_TRUE(res.ok());
  EXPECT_EQ(res.get(), 100);

  StreamReader reader{buffer.data(), buffer.size()};
  checkLogEntry(reader, LogIndex{100}, LogTerm{1}, EntryType::wMeta, payload);
}

TEST_F(LogPersistorTest, getIterator) {
  LogPersistor persistor{LogId{42}, walManager};

  auto normalPayload = LogPayload::createFromString("dummyPayload");
  auto metaPayload = LogMetaPayload::withPing("message");

  auto log = InMemoryLog{}.append(
      {InMemoryLogEntry{LogEntry{LogTerm{1}, LogIndex{100}, normalPayload}},
       InMemoryLogEntry{LogEntry{LogTerm{1}, LogIndex{101}, metaPayload}}});

  auto res =
      persistor.insert(log.getLogIterator(), LogPersistor::WriteOptions{})
          .get();
  ASSERT_TRUE(res.ok());
  EXPECT_EQ(res.get(), 101);

  auto iter =
      persistor.getIterator(IteratorPosition::fromLogIndex(LogIndex{0}));
  ASSERT_NE(nullptr, iter);
  auto entry = iter->next();
  {
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(LogIndex{100}, entry->entry().logIndex());
    EXPECT_EQ(LogTerm{1}, entry->entry().logTerm());
    auto* payload = entry->entry().logPayload();
    ASSERT_NE(nullptr, payload);
    ASSERT_EQ(normalPayload.byteSize(), payload->byteSize());
    ASSERT_TRUE(normalPayload.slice().binaryEquals(payload->slice()));
  }

  entry = iter->next();
  {
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(LogIndex{101}, entry->entry().logIndex());
    EXPECT_EQ(LogTerm{1}, entry->entry().logTerm());
    auto* payload = entry->entry().meta();
    ASSERT_NE(nullptr, payload);
    ASSERT_TRUE(std::holds_alternative<LogMetaPayload::Ping>(payload->info));
    auto expected = std::get<LogMetaPayload::Ping>(metaPayload.info);
    auto actual = std::get<LogMetaPayload::Ping>(payload->info);
    EXPECT_EQ(expected.message, actual.message);
    EXPECT_EQ(std::chrono::floor<std::chrono::seconds>(expected.time),
              actual.time);
  }

  entry = iter->next();
  EXPECT_FALSE(entry.has_value());
}

TEST_F(LogPersistorTest, getIterator_seeks_to_log_index) {
  LogPersistor persistor{LogId{42}, walManager};

  auto normalPayload = LogPayload::createFromString("dummyPayload");
  auto metaPayload = LogMetaPayload::withPing("message");

  auto log = InMemoryLog{}.append(
      {InMemoryLogEntry{LogEntry{LogTerm{1}, LogIndex{100}, normalPayload}},
       InMemoryLogEntry{LogEntry{LogTerm{1}, LogIndex{101}, metaPayload}}});

  auto res =
      persistor.insert(log.getLogIterator(), LogPersistor::WriteOptions{})
          .get();
  ASSERT_TRUE(res.ok());
  EXPECT_EQ(res.get(), 101);

  auto iter =
      persistor.getIterator(IteratorPosition::fromLogIndex(LogIndex{101}));
  ASSERT_NE(nullptr, iter);
  auto entry = iter->next();
  {
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(LogIndex{101}, entry->entry().logIndex());
    EXPECT_EQ(LogTerm{1}, entry->entry().logTerm());
    auto* payload = entry->entry().meta();
    ASSERT_NE(nullptr, payload);
    ASSERT_TRUE(std::holds_alternative<LogMetaPayload::Ping>(payload->info));
    auto expected = std::get<LogMetaPayload::Ping>(metaPayload.info);
    auto actual = std::get<LogMetaPayload::Ping>(payload->info);
    EXPECT_EQ(expected.message, actual.message);
    EXPECT_EQ(std::chrono::floor<std::chrono::seconds>(expected.time),
              actual.time);
  }

  entry = iter->next();
  EXPECT_FALSE(entry.has_value());
}

}  // namespace arangodb::replication2::storage::wal::test