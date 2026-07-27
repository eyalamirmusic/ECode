#pragma once

#include "ScrollOffset.h"
#include "Style.h"
#include "TextFile.h"

#include <eacp/Core/Core.h>

#include <functional>

namespace ecode
{
// Whether a tab closed, and if not, why not.
enum class CloseResult
{
    closed,

    // The file has edits that were never written. Nothing was closed; see
    // Workspace::close.
    hasUnsavedChanges,
};

// One file open at once: the file itself, how it is coloured, and where the
// view of it had got to.
struct OpenFile
{
    TextFile file;

    // One parse tree per document rather than one for the workspace. A shared
    // highlighter would have to reparse on every switch — ~40 ms on an 8,000
    // line file, paid on a ⌘W rather than on an open — and would hand the
    // renderer a tree describing text that is no longer on screen in between.
    //
    // Null is a valid state and means "draw this as plain text", which is what
    // a grammar that failed to load leaves behind.
    eacp::OwningPointer<Highlighter> highlighter;

    // The scroll offset belongs to the file rather than to the widget showing
    // it, so switching away and back leaves the text where it was left.
    //
    // Kept here rather than saved and restored around each switch for the same
    // reason Editor owns the line map: a step that has to be remembered at
    // every call site is one that will eventually be forgotten, and forgetting
    // it means the view jumping on a tab switch with nothing to say why.
    ScrollOffset scroll;
};

// Every file open at once, and which of them is being looked at.
//
// The set is never empty. Closing the last tab leaves an empty untitled file
// rather than nothing, because everything downstream — the window title, the
// status bar, the renderer, every editing command — is written against "the
// active file", and an absent one is a null check at each of them plus a window
// with no caret, which reads as broken rather than as empty.
class Workspace
{
public:
    // How a document gets its colours. Taken at construction rather than set
    // afterwards, because the workspace makes its first file in its own
    // constructor — a factory installed later would leave that one file, the
    // very one a launch with no readable path lands in, permanently uncoloured
    // while every file opened after it was fine.
    //
    // Injected at all so this stays free of tree-sitter: ECodeCore knows the
    // Highlighter interface and nothing about who implements it. The default
    // returns null, and everything draws as plain text.
    using HighlighterFactory = std::function<eacp::OwningPointer<Highlighter>()>;

    explicit Workspace(HighlighterFactory factory = noHighlighting());

    static HighlighterFactory noHighlighting()
    {
        return [] { return eacp::OwningPointer<Highlighter> {}; };
    }

    int count() const { return files.size(); }
    int activeIndex() const { return current; }

    OpenFile& at(int index);
    const OpenFile& at(int index) const;

    OpenFile& active() { return at(activeIndex()); }
    const OpenFile& active() const { return at(activeIndex()); }

    Editor& editor() { return active().file.editor(); }
    const Editor& editor() const { return active().file.editor(); }

    // Opens the file, or — if it is already open — activates the tab showing
    // it. Two tabs over one path would be two undo histories and two dirty
    // flags over one set of bytes on disk, and whichever saved last would win
    // silently.
    //
    // False when the file cannot be read, in which case nothing changes.
    bool open(const eacp::FilePath& path);

    // A new empty buffer with no path, activated. It has nowhere to save to
    // until saveAs names one.
    OpenFile& addUntitled();

    // Refuses a file with unsaved edits rather than discarding them, the same
    // way TextFile::save refuses to clobber someone else's write: which version
    // wins is a question only a person can answer, and there is no dialog to
    // ask in. The caller either saves or asks again through closeDiscarding.
    CloseResult close(int index);
    void closeDiscarding(int index);

    // --- moving a file between groups ------------------------------------
    //
    // The pair EditorGroups moves a tab with. Ownership is handed over rather
    // than the file being copied out and back in, and that is the whole reason
    // these exist as a pair instead of as a re-open in the destination: an
    // OpenFile that changes address takes its Editor with it, and connect()
    // captured that editor by pointer — so a file rebuilt on the other side
    // would arrive with its undo history, its syntax tree and its dirty flag
    // reset, and with the old group's callbacks pointing into freed memory.
    // Moving the OwningPointer leaves every address exactly where it was.

    // Removes the file at `index` and hands it over. Null for an index that
    // names no tab. The set is still never empty afterwards, so taking the only
    // file leaves the same untitled buffer closing the last tab does.
    eacp::OwningPointer<OpenFile> take(int index);

    // Takes ownership of a file another group let go of, and activates it.
    // Replaces a scratch buffer rather than sitting beside one, the same rule
    // open() follows — otherwise a file moved into a freshly split group lands
    // next to the dead "Untitled" the split created.
    void adopt(eacp::OwningPointer<OpenFile> incoming);

    void activate(int index);

    // Wrap at both ends, so ⌃Tab through a workspace of two is a toggle.
    void activateNext();
    void activatePrevious();

    // The tab showing this path, or -1. Paths are compared after resolving
    // symlinks and `..`, so opening the same file by two spellings finds the
    // one tab rather than making a second.
    int indexOf(const eacp::FilePath& path) const;

    // True when any open file has edits that were never written — what a "quit
    // anyway?" question needs to know.
    bool hasUnsavedChanges() const;

    // Fired whenever the set of tabs or which one is active changes, so the
    // chrome follows without every command that opens or closes a file
    // remembering to update it.
    std::function<void()> onChanged = [] {};

private:
    // An untitled buffer nobody has typed in. Opening a file replaces one of
    // these rather than adding beside it: the workspace always has a tab, so
    // otherwise every launch would show a dead "Untitled" next to the file that
    // was actually asked for.
    bool isScratch(const OpenFile& entry) const;

    OpenFile& insertAfterActive();

    // Puts the untitled buffer back after the last tab goes, connected like any
    // other. Shared by close and take rather than written at each, because the
    // half that is easy to leave out is the connect: a buffer with no
    // highlighter draws plain forever and looks exactly like a file whose
    // language has no grammar. Same failure the constructor's factory argument
    // exists to prevent, one method along.
    void refillIfEmpty();

    // Points a fresh entry's editor at its own highlighter, so an edit reparses
    // that document's tree and a wholesale replacement resets it.
    void connect(OpenFile& entry);

    HighlighterFactory makeHighlighter;

    eacp::OwnedVector<OpenFile> files;

    int current = 0;
};
} // namespace ecode
