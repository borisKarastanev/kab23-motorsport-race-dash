.pragma library

// Post-delete navigation for the session Details view. `view` is the settings
// StackView with the session-summary page currently on top (pushed as:
// tracks-list → track's-sessions → session-summary). When the just-deleted
// session's track still has other sessions, pop one step back to that track's
// remaining-sessions list; when it was the last one, pop all the way to the
// track-groups ("Sessions") list so the user never lands on an empty per-track
// list for a track that no longer exists.
function popAfterDelete(view, sessionModel, trackId) {
    // Always pop off the summary; when the track has no sessions left, pop the
    // now-empty track list too. Two relative pops rather than one pop to an
    // absolute index, so nothing pushed below this 3-page chain can shift the
    // target.
    view.pop()                                          // off the summary → the track's list
    if (!sessionModel.hasSessionsForTrack(trackId))
        view.pop()                                      // last one gone → the Sessions (tracks) list
}
