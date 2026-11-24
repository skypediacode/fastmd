#include "ImportManager.h"

#include "WorkspaceFileOps.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>

namespace {
// AnyDoc applies its own resource limits, but a converter that wedges (a
// pathological PDF, a stalled read off a disconnected share) would otherwise
// never report back: nothing else ever ends this QProcess. Generous enough that
// a legitimately slow conversion is never cut short.
constexpr int kConversionTimeoutMs = 5 * 60 * 1000;
}

ImportManager::ImportManager(QObject* parent)
    : QObject(parent)
{
}

ImportManager::~ImportManager()
{
    // m_process is a child QObject, so without this ~QProcess would run with the
    // converter still alive -- and that destructor kills it and then calls
    // waitForFinished() with no timeout. On the application's teardown path that
    // is an unbounded block with no window left to explain it.
    killProcess();
}

void ImportManager::killProcess()
{
    if (!m_process)
        return;
    QProcess* process = m_process;
    m_process = nullptr;
    process->disconnect(this);
    if (process->state() != QProcess::NotRunning) {
        process->kill();
        process->waitForFinished(2000);
    }
    delete process;
}

QString ImportManager::converterPath()
{
    return QCoreApplication::applicationDirPath()
        + QStringLiteral("/tools/anydoc/fastmd-anydoc.exe");
}

const QStringList& ImportManager::supportedExtensions()
{
    static const QStringList extensions = {
        QStringLiteral("doc"),  QStringLiteral("docx"), QStringLiteral("docm"),
        QStringLiteral("ppt"),  QStringLiteral("pps"),  QStringLiteral("pot"),
        QStringLiteral("pptx"), QStringLiteral("pptm"), QStringLiteral("ppsx"),
        QStringLiteral("ppsm"), QStringLiteral("xls"),  QStringLiteral("xlsx"),
        QStringLiteral("xlsm"), QStringLiteral("xlsb"), QStringLiteral("odt"),
        QStringLiteral("ods"),  QStringLiteral("odp"),  QStringLiteral("rtf"),
        QStringLiteral("epub"), QStringLiteral("csv"),  QStringLiteral("pdf"),
    };
    return extensions;
}

// Extension, not content: .rtf and .csv are plain text but are deliberately
// converted rather than pasted verbatim, so sniffing would misroute them.
bool ImportManager::handlesExtension(const QString& suffix)
{
    if (suffix.isEmpty())
        return false;
    const QString lowered = suffix.toLower();
    return supportedExtensions().contains(lowered);
}

QString ImportManager::documentFilterEntry()
{
    QStringList patterns;
    patterns.reserve(supportedExtensions().size());
    for (const QString& extension : supportedExtensions())
        patterns.append(QStringLiteral("*.") + extension);
    return QObject::tr("Documents supported by AnyDoc (%1)").arg(patterns.join(QLatin1Char(' ')));
}

QString ImportManager::importFileFilter()
{
    return documentFilterEntry() + QStringLiteral(";;") + QObject::tr("All Files (*)");
}

QString ImportManager::destinationFor(const QString& inputPath)
{
    const QFileInfo input(inputPath);
    QString folder = input.absolutePath();
    // A document opened from a read-only location (an archive mount, Program
    // Files, a locked share) would otherwise dead-end on an io error.
    if (!QFileInfo(folder).isWritable()) {
        folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        if (folder.isEmpty())
            return QString();
    }
    const QString desired = QDir(folder).filePath(input.completeBaseName() + QStringLiteral(".md"));
    return WorkspaceFileOps::uniqueDestinationPath(desired);
}

// The converter refuses a destination that already exists, so the scratch name
// only has to be unique -- its own folder makes that true without a probe loop.
QString ImportManager::temporaryDestinationFor(const QString& inputPath)
{
    const QString folder = QDir(QDir::tempPath())
        .filePath(QStringLiteral("fastmd-insert-")
                  + QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!QDir().mkpath(folder))
        return QString();
    QString base = QFileInfo(inputPath).completeBaseName();
    if (base.isEmpty())
        base = QStringLiteral("content");
    return QDir(folder).filePath(base + QStringLiteral(".md"));
}

void ImportManager::removeTemporary(const QString& markdownPath)
{
    if (markdownPath.isEmpty())
        return;
    QFile::remove(markdownPath);
    QDir folder = QFileInfo(markdownPath).absoluteDir();
    if (folder.dirName().startsWith(QStringLiteral("fastmd-insert-")))
        folder.removeRecursively();
}

bool ImportManager::isRunning() const
{
    return m_process != nullptr;
}

QString ImportManager::failureTitle(Purpose purpose)
{
    return purpose == Purpose::InsertContent ? tr("Insert failed") : tr("Import failed");
}

bool ImportManager::start(Purpose purpose, const QString& inputPath, const QString& markdownPath)
{
    if (m_process)
        return false;
    if (!QFileInfo::exists(converterPath())) {
        emit conversionFailed(purpose, tr("Converter unavailable"),
            tr("The FastMD document converter is missing. Reinstall FastMD or use a complete release package."));
        return false;
    }
    if (!QFileInfo(inputPath).isFile() || markdownPath.isEmpty()) {
        emit conversionFailed(purpose, failureTitle(purpose),
                              tr("The selected import paths are not valid."));
        return false;
    }

    m_purpose = purpose;
    m_markdownPath = markdownPath;
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this](int, QProcess::ExitStatus) { finishProcess(); });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finishProcess();
    });
    m_process->start(converterPath(), {
        QStringLiteral("--input"), inputPath,
        QStringLiteral("--output"), markdownPath,
    });

    m_timedOut = false;
    if (!m_timeout) {
        m_timeout = new QTimer(this);
        m_timeout->setSingleShot(true);
        connect(m_timeout, &QTimer::timeout, this, [this]() {
            if (!m_process)
                return;
            m_timedOut = true;
            // Kills the converter, which makes finished() fire and the normal
            // failure path report it -- rather than leaving the caller waiting
            // on a signal that is never coming.
            m_process->kill();
        });
    }
    m_timeout->start(kConversionTimeoutMs);
    return true;
}

void ImportManager::finishProcess()
{
    if (!m_process)
        return;
    if (m_timeout)
        m_timeout->stop();
    QProcess* process = m_process;
    m_process = nullptr;
    const QByteArray stdoutData = process->readAllStandardOutput();
    const QString stderrText = QString::fromUtf8(process->readAllStandardError()).trimmed();
    const QJsonDocument json = QJsonDocument::fromJson(stdoutData);
    const QJsonObject result = json.isObject() ? json.object() : QJsonObject{};
    const bool succeeded = process->exitStatus() == QProcess::NormalExit
        && process->exitCode() == 0 && result.value(QStringLiteral("ok")).toBool();
    process->deleteLater();

    if (succeeded) {
        emit converted(m_purpose, m_markdownPath, result.value(QStringLiteral("droppedImages")).toInt());
        return;
    }

    if (m_timedOut) {
        m_timedOut = false;
        emit conversionFailed(m_purpose, failureTitle(m_purpose),
            tr("The document converter did not finish in time and was stopped."));
        return;
    }

    const QString code = result.value(QStringLiteral("code")).toString();
    const QString detail = result.value(QStringLiteral("message")).toString();
    emit conversionFailed(m_purpose, failureTitle(m_purpose),
                          errorMessage(code, detail.isEmpty() ? stderrText : detail));
}

// Codes come from AnyDoc's ConvertError::code() plus the wrapper's own. They
// are matched as strings, so a renamed code degrades to the raw English
// detail rather than failing to build - see dev-docs/BUNDLED_TOOLS.md.
QString ImportManager::errorMessage(const QString& code, const QString& detail)
{
    if (code == QStringLiteral("unsupported"))
        return QObject::tr("This file format is not supported, or the PDF contains no selectable text.");
    if (code == QStringLiteral("encrypted"))
        return QObject::tr("This document is password-protected and cannot be imported.");
    if (code == QStringLiteral("resourceLimit"))
        return QObject::tr("This document exceeds the converter's safety limits.");
    if (code == QStringLiteral("malformed") || code == QStringLiteral("missingPart"))
        return QObject::tr("This document is damaged or incomplete, and could not be read.");
    if (code == QStringLiteral("outputConflict"))
        return QObject::tr("A file appeared at the import destination while converting. Try again.");
    return detail.isEmpty() ? QObject::tr("The document could not be converted.") : detail;
}
