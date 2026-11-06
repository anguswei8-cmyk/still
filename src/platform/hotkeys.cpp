#include "platform.h"

namespace still {

bool Hotkeys::bind(int id, const QKeySequence &sequence) {
    if (!staged_.isEmpty() || !stage(id, sequence)) return false;
    commitStaged();
    return true;
}

bool Hotkeys::stage(int id, const QKeySequence &sequence) {
    if (staged_.contains(id)) return staged_.value(id).sequence == sequence;
    if (sequences_.value(id) == sequence) return true;
    static int nextNativeId = 1000;
    Reservation pending{sequence, 0, ++nextNativeId};
    if (!sequence.isEmpty()) {
        pending.handle = registerSequence(sequence, pending.nativeId);
        if (!pending.handle) return false;
    }
    staged_[id] = pending;
    return true;
}

void Hotkeys::commitStaged() {
    for (auto it = staged_.cbegin(); it != staged_.cend(); ++it) {
        unbind(it.key());
        if (it->handle) {
            registrations_[it.key()] = it->handle;
            nativeIds_[it.key()] = it->nativeId;
            sequences_[it.key()] = it->sequence;
        }
    }
    staged_.clear();
}

void Hotkeys::discardStaged() {
    for (const auto &pending : staged_) if (pending.handle) releaseRegistration(pending.handle);
    staged_.clear();
}

void Hotkeys::unbind(int id) {
    if (registrations_.contains(id)) releaseRegistration(registrations_.take(id));
    nativeIds_.remove(id);
    sequences_.remove(id);
}

bool Hotkeys::dispatchNative(int nativeId) {
    const int id = nativeIds_.key(nativeId, -1);
    if (id < 0) return false; // Reserved replacements must not trigger before settings commit.
    dispatch(id);
    return true;
}

} // namespace still
