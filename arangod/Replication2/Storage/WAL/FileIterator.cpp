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

#include "FileIterator.h"

#include "Replication2/Storage/WAL/Entry.h"
#include "Replication2/Storage/WAL/IFileReader.h"

#include "velocypack/Buffer.h"

namespace arangodb::replication2::storage::wal {

FileIterator::FileIterator(IteratorPosition position,
                           std::unique_ptr<IFileReader> reader)
    : _pos(position), _reader(std::move(reader)) {
  _reader->seek(_pos.fileOffset());
  moveToFirstEntry();
}

void FileIterator::moveToFirstEntry() {
  auto pos = _reader->position();
  auto idx = _pos.index();
  Entry::CompressedHeader compressedHeader;
  while (_reader->read(compressedHeader)) {
    auto header = Entry::Header::fromCompressed(compressedHeader);
    if (header.index >= idx.value) {
      _reader->seek(pos);  // reset to start of entry
      _pos = IteratorPosition::withFileOffset(LogIndex{header.index}, pos);
      break;
    }
    _reader->seek(pos + sizeof(Entry::CompressedHeader) +
                  Entry::paddedPayloadSize(header.size) +
                  sizeof(Entry::Footer));
    pos = _reader->position();
  }
  _pos = IteratorPosition::withFileOffset(idx, pos);
}

auto FileIterator::next() -> std::optional<PersistedLogEntry> {
  TRI_ASSERT(_pos.fileOffset() == _reader->position());

  Entry::CompressedHeader compressedHeader;
  if (!_reader->read(compressedHeader)) {
    return std::nullopt;
  }

  auto header = Entry::Header::fromCompressed(compressedHeader);

  auto paddedSize = Entry::paddedPayloadSize(header.size);
  velocypack::UInt8Buffer buffer(paddedSize);
  if (!_reader->read(buffer.data(), paddedSize)) {
    return std::nullopt;
  }
  buffer.resetTo(header.size);

  auto entry = [&]() -> LogEntry {
    if (header.type == EntryType::wMeta) {
      auto payload =
          LogMetaPayload::fromVelocyPack(velocypack::Slice(buffer.data()));
      return LogEntry(LogTerm(header.term), LogIndex(header.index),
                      std::move(payload));
    } else {
      LogPayload payload(std::move(buffer));
      return LogEntry(LogTerm(header.term), LogIndex(header.index),
                      std::move(payload));
    }
  }();

  Entry::Footer footer;
  if (!_reader->read(footer)) {
    return std::nullopt;
  }
  TRI_ASSERT(footer.size % 8 == 0);
  TRI_ASSERT(footer.size == _reader->position() - _pos.fileOffset());

  _pos =
      IteratorPosition::withFileOffset(entry.logIndex(), _reader->position());
  return PersistedLogEntry(std::move(entry), _pos);
}

}  // namespace arangodb::replication2::storage::wal
