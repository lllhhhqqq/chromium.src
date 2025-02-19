// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SESSIONS_CORE_BASE_SESSION_SERVICE_H_
#define COMPONENTS_SESSIONS_CORE_BASE_SESSION_SERVICE_H_

#include "base/callback.h"
#include "base/files/file_path.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/scoped_vector.h"
#include "base/memory/weak_ptr.h"
#include "base/task/cancelable_task_tracker.h"
#include "components/sessions/core/sessions_export.h"
#include "url/gurl.h"


namespace sessions {
class BaseSessionServiceDelegate;
class SerializedNavigationEntry;
class SessionCommand;
class SessionBackend;

// BaseSessionService is the super class of both tab restore service and
// session service. It contains commonality needed by both, in particular
// it manages a set of SessionCommands that are periodically sent to a
// SessionBackend.
class SESSIONS_EXPORT BaseSessionService {
 public:
  // Identifies the type of session service this is. This is used by the
  // backend to determine the name of the files.
  enum SessionType {
    SESSION_RESTORE,
    TAB_RESTORE
  };

  typedef base::Callback<void(ScopedVector<SessionCommand>)>
      GetCommandsCallback;

  // Creates a new BaseSessionService. After creation you need to invoke
  // Init. |delegate| will remain owned by the creator and it is guaranteed
  // that its lifetime surpasses this class.
  // |type| gives the type of session service, |path| the path to save files to.
  BaseSessionService(SessionType type,
                     const base::FilePath& path,
                     BaseSessionServiceDelegate* delegate);
  ~BaseSessionService();

  // Moves the current session to the last session.
  void MoveCurrentSessionToLastSession();

  // Deletes the last session.
  void DeleteLastSession();

  // Returns the set of commands which were scheduled to be written. Once
  // committed to the backend, the commands are removed from here.
  const ScopedVector<SessionCommand>& pending_commands() {
    return pending_commands_;
  }

  // Whether the next save resets the file before writing to it.
  void set_pending_reset(bool value) { pending_reset_ = value; }
  bool pending_reset() const { return pending_reset_; }

  // Returns the number of commands sent down since the last reset.
  int commands_since_reset() const { return commands_since_reset_; }

  // Schedules a command. This adds |command| to pending_commands_ and
  // invokes StartSaveTimer to start a timer that invokes Save at a later
  // time.
  void ScheduleCommand(scoped_ptr<SessionCommand> command);

  // Appends a command as part of a general rebuild. This will neither count
  // against a rebuild, nor will it trigger a save of commands.
  void AppendRebuildCommand(scoped_ptr<SessionCommand> command);

  // Erase the |old_command| from the list of commands.
  // The passed command will automatically be deleted.
  void EraseCommand(SessionCommand* old_command);

  // Swap a |new_command| into the list of queued commands at the location of
  // the |old_command|. The |old_command| will be automatically deleted in the
  // process.
  void SwapCommand(SessionCommand* old_command,
                   scoped_ptr<SessionCommand> new_command);

  // Clears all commands from the list.
  void ClearPendingCommands();

  // Starts the timer that invokes Save (if timer isn't already running).
  void StartSaveTimer();

  // Passes all pending commands to the backend for saving.
  void Save();

  // Uses the backend to load the last session commands from disc. |callback|
  // gets called once the data has arrived.
  base::CancelableTaskTracker::TaskId ScheduleGetLastSessionCommands(
      const GetCommandsCallback& callback,
      base::CancelableTaskTracker* tracker);

 private:
  friend class BaseSessionServiceTestHelper;

  // This posts the task to the SequencedWorkerPool, or run immediately
  // if the SequencedWorkerPool has been shutdown.
  void RunTaskOnBackendThread(const tracked_objects::Location& from_here,
                              const base::Closure& task);

  // The backend object which reads and saves commands.
  scoped_refptr<SessionBackend> backend_;

  // Commands we need to send over to the backend.
  ScopedVector<SessionCommand> pending_commands_;

  // Whether the backend file should be recreated the next time we send
  // over the commands.
  bool pending_reset_;

  // The number of commands sent to the backend before doing a reset.
  int commands_since_reset_;

  BaseSessionServiceDelegate* delegate_;

  // A token to make sure that all tasks will be serialized.
  base::SequencedWorkerPool::SequenceToken sequence_token_;

  // Used to invoke Save.
  base::WeakPtrFactory<BaseSessionService> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(BaseSessionService);
};

}  // namespace sessions

#endif  // COMPONENTS_SESSIONS_CORE_BASE_SESSION_SERVICE_H_
