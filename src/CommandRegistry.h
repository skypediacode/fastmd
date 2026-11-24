#pragma once

#include <QKeySequence>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

class QAction;

// One user-facing action exposed by the Command Palette.
//
// A command is a thin description plus a callback: it never re-implements
// behavior. Commands are usually created from an existing QAction (see
// CommandRegistry::registerAction), so the palette, the menu item and the
// toolbar button all drive the same code, shortcut and enabled state.
struct Command {
    QString id;                 // stable, unique; used for de-duplication
    QString title;              // "Toggle Preview"
    QString category;           // "View"
    QKeySequence shortcut;      // optional
    QStringList keywords;       // optional aliases/synonyms ("wrap", "h1", "pdf")
    std::function<void()> run;
    std::function<bool()> enabled;  // null == always enabled
};

// A command that survived a query, with the score used to rank it and the
// positions inside `title` that the query matched (for highlighting).
struct CommandMatch {
    const Command* command = nullptr;
    int score = 0;
    QVector<int> titleHits;
};

// Registry + search index for Command Palette commands.
//
// Adding a command never requires touching the palette UI: register it here
// and it shows up. Registration is idempotent per id -- the last registration
// wins -- so a command can be re-registered when its backing action changes.
class CommandRegistry : public QObject {
    Q_OBJECT
public:
    explicit CommandRegistry(QObject* parent = nullptr);

    void registerCommand(Command command);

    // Derives title, shortcut, enabled state and the run callback from an
    // existing QAction. Mnemonic '&' markers and trailing ellipses are
    // stripped from the title. A checkable action keeps its own toggle
    // semantics because it is invoked through QAction::trigger().
    void registerAction(const QString& id, const QString& category, QAction* action,
                        const QStringList& keywords = {});

    bool contains(const QString& id) const;
    const QVector<Command>& commands() const { return m_commands; }

    // Ranked matches for `query`. An empty query returns every command in
    // registration order (categories stay grouped as registered).
    QVector<CommandMatch> search(const QString& query) const;

    // Scores `text` against an already lower-cased `query`. Returns -1 when
    // the query cannot match at all. Exposed for tests.
    static int score(const QString& text, const QString& loweredQuery, QVector<int>* hits);

private:
    QVector<Command> m_commands;
};
