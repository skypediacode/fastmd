#include "CommandRegistry.h"

#include <QAction>

#include <algorithm>

namespace {

bool isWordStart(const QString& text, int index)
{
    if (index <= 0)
        return true;
    const QChar previous = text.at(index - 1);
    return !previous.isLetterOrNumber();
}

QString cleanActionText(const QString& raw)
{
    QString text = raw;
    text.remove(QLatin1Char('&'));
    text = text.trimmed();
    while (text.endsWith(QChar(0x2026)) || text.endsWith(QLatin1Char('.')))
        text.chop(1);
    return text.trimmed();
}

// Penalties applied to a match found somewhere other than the command title,
// so an exact title hit always outranks an incidental keyword hit.
constexpr int CategoryPenalty = 40;
constexpr int KeywordPenalty  = 70;

} // namespace

CommandRegistry::CommandRegistry(QObject* parent) : QObject(parent) {}

void CommandRegistry::registerCommand(Command command)
{
    if (command.id.isEmpty() || command.title.isEmpty() || !command.run)
        return;
    for (Command& existing : m_commands) {
        if (existing.id == command.id) {
            existing = std::move(command);
            return;
        }
    }
    m_commands.append(std::move(command));
}

void CommandRegistry::registerAction(const QString& id, const QString& category, QAction* action,
                                     const QStringList& keywords)
{
    if (!action)
        return;
    const QString title = cleanActionText(action->text());
    if (title.isEmpty())
        return;

    Command command;
    command.id = id;
    command.title = title;
    command.category = category;
    command.shortcut = action->shortcut();
    command.keywords = keywords;
    // QPointer semantics are unnecessary here: every action the palette wraps
    // is parented to the MainWindow that owns the registry, so neither can
    // outlive the other.
    command.run = [action]() { action->trigger(); };
    command.enabled = [action]() { return action->isEnabled(); };
    registerCommand(std::move(command));
}

bool CommandRegistry::contains(const QString& id) const
{
    return std::any_of(m_commands.cbegin(), m_commands.cend(),
                       [&id](const Command& c) { return c.id == id; });
}

int CommandRegistry::score(const QString& text, const QString& loweredQuery, QVector<int>* hits)
{
    if (hits)
        hits->clear();
    if (loweredQuery.isEmpty())
        return 0;
    if (text.isEmpty())
        return -1;

    const QString lower = text.toLower();

    auto fill = [hits](int start, int length) {
        if (!hits)
            return;
        hits->reserve(length);
        for (int i = 0; i < length; ++i)
            hits->append(start + i);
    };

    if (lower == loweredQuery) {
        fill(0, loweredQuery.size());
        return 1000;
    }

    const int index = lower.indexOf(loweredQuery);
    if (index >= 0) {
        fill(index, loweredQuery.size());
        if (index == 0)
            return 900;
        return (isWordStart(lower, index) ? 800 : 700) - qMin(index, 200);
    }

    // Subsequence fallback: every query character must appear in order.
    // Consecutive runs and characters that open a word score higher, gaps
    // cost, so "gtl" ranks "Go To Line" above "Get Total Length".
    int cursor = 0;
    int total = 300;
    int previousMatch = -2;
    QVector<int> found;
    found.reserve(loweredQuery.size());
    for (const QChar qc : loweredQuery) {
        if (qc.isSpace())
            continue;
        const int at = lower.indexOf(qc, cursor);
        if (at < 0)
            return -1;
        if (at == previousMatch + 1)
            total += 12;
        if (isWordStart(lower, at))
            total += 10;
        total -= qMin(at - cursor, 8);
        found.append(at);
        previousMatch = at;
        cursor = at + 1;
    }
    if (found.isEmpty())
        return 0;
    if (hits)
        *hits = found;
    return total;
}

QVector<CommandMatch> CommandRegistry::search(const QString& query) const
{
    const QString needle = query.trimmed().toLower();

    QVector<CommandMatch> results;
    results.reserve(m_commands.size());

    if (needle.isEmpty()) {
        for (const Command& command : m_commands)
            results.append({ &command, 0, {} });
        return results;
    }

    for (const Command& command : m_commands) {
        QVector<int> hits;
        int best = score(command.title, needle, &hits);
        QVector<int> bestHits = hits;

        const int categoryScore =
            score(command.category + QLatin1Char(' ') + command.title, needle, nullptr);
        if (categoryScore >= 0 && categoryScore - CategoryPenalty > best) {
            best = categoryScore - CategoryPenalty;
            bestHits.clear();
        }

        for (const QString& keyword : command.keywords) {
            const int keywordScore = score(keyword, needle, nullptr);
            if (keywordScore >= 0 && keywordScore - KeywordPenalty > best) {
                best = keywordScore - KeywordPenalty;
                bestHits.clear();
            }
        }

        if (best >= 0)
            results.append({ &command, best, bestHits });
    }

    std::stable_sort(results.begin(), results.end(),
                     [](const CommandMatch& a, const CommandMatch& b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.command->title.size() < b.command->title.size();
    });
    return results;
}
