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
/// @author Frank Celler
/// @author Achim Brandt
/// @author Jan Steemann
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Basics/Common.h"
#include "Basics/Locking.h"
#include "Basics/debugging.h"

#include <thread>

/// @brief construct locker with file and line information
#define WRITE_LOCKER(obj, lock)                                            \
  arangodb::basics::WriteLocker<typename std::decay<decltype(lock)>::type> \
      obj(&lock, arangodb::basics::LockerType::BLOCKING, true, __FILE__,   \
          __LINE__)

#define WRITE_LOCKER_EVENTUAL(obj, lock)                                   \
  arangodb::basics::WriteLocker<typename std::decay<decltype(lock)>::type> \
      obj(&lock, arangodb::basics::LockerType::EVENTUAL, true, __FILE__,   \
          __LINE__)

#define TRY_WRITE_LOCKER(obj, lock)                                        \
  arangodb::basics::WriteLocker<typename std::decay<decltype(lock)>::type> \
      obj(&lock, arangodb::basics::LockerType::TRY, true, __FILE__, __LINE__)

#define CONDITIONAL_WRITE_LOCKER(obj, lock, condition)                     \
  arangodb::basics::WriteLocker<typename std::decay<decltype(lock)>::type> \
      obj(&lock, arangodb::basics::LockerType::BLOCKING, (condition),      \
          __FILE__, __LINE__)

namespace arangodb::basics {

/// @brief write locker
/// A WriteLocker write-locks a read-write lock during its lifetime and unlocks
/// the lock when it is destroyed.
template<class LockType>
class WriteLocker {
 public:
  WriteLocker(WriteLocker const&) = delete;
  WriteLocker& operator=(WriteLocker const&) = delete;

  /// @brief acquires a write-lock
  /// The constructors acquire a write lock, the destructor unlocks the lock.
  WriteLocker(LockType* readWriteLock, LockerType type, bool condition,
              char const* file, int line)
      : _readWriteLock(readWriteLock),
        _file(file),
        _line(line),
        _isLocked(false) {
    if (condition) {
      if (type == LockerType::BLOCKING) {
        lock();
        TRI_ASSERT(_isLocked);
      } else if (type == LockerType::EVENTUAL) {
        lockEventual();
        TRI_ASSERT(_isLocked);
      } else if (type == LockerType::TRY) {
        _isLocked = tryLock();
      }
    }
  }

  /// @brief releases the write-lock
  ~WriteLocker() noexcept {
    if (_isLocked) {
      // cppcheck-suppress *
      static_assert(noexcept(_readWriteLock->unlockWrite()));
      _readWriteLock->unlockWrite();
    }
  }

  /// @brief whether or not we acquired the lock
  [[nodiscard]] bool isLocked() const noexcept { return _isLocked; }

  /// @brief eventually acquire the write lock
  void lockEventual() {
    while (!tryLock()) {
      std::this_thread::yield();
    }
    TRI_ASSERT(_isLocked);
  }

  [[nodiscard]] bool tryLock() {
    TRI_ASSERT(!_isLocked);
    if (_readWriteLock->tryLockWrite()) {
      _isLocked = true;
    }
    return _isLocked;
  }

  /// @brief acquire the write lock, blocking
  void lock() {
    TRI_ASSERT(!_isLocked);
    _readWriteLock->lockWrite();
    _isLocked = true;
  }

  /// @brief unlocks the lock if we own it
  bool unlock() {
    if (_isLocked) {
      _readWriteLock->unlockWrite();
      _isLocked = false;
      return true;
    }
    return false;
  }

  /// @brief steals the lock, but does not unlock it
  bool steal() {
    if (_isLocked) {
      _isLocked = false;
      return true;
    }
    return false;
  }

 private:
  /// @brief the read-write lock
  LockType* _readWriteLock;

  /// @brief file
  char const* _file;

  /// @brief line number
  int _line;

  /// @brief whether or not the lock was acquired
  bool _isLocked;
};

}  // namespace arangodb::basics
