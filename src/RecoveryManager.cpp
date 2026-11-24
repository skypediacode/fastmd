#include "RecoveryManager.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QCryptographicHash>

namespace {
const quint32 kMagic = 0x464D4452;
const quint16 kVersion = 3;
}

QString RecoveryManager::recoveryDirectory()
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/recovery");
    QDir().mkpath(path);
    return path;
}

QString RecoveryManager::keyFor(const QString& path, const QString& recoveryId)
{
    const QByteArray source = (path.isEmpty() ? recoveryId : path).toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(source, QCryptographicHash::Sha256).toHex());
}

bool RecoveryManager::write(const RecoverySnapshot& snapshot)
{
    QSaveFile file(recoveryDirectory() + QLatin1Char('/') + snapshot.key + QStringLiteral(".recovery"));
    if (!file.open(QIODevice::WriteOnly))
        return false;

    QDataStream stream(&file);
    stream << kMagic << kVersion << snapshot.path << snapshot.content
           << snapshot.lineEnding << static_cast<qint32>(snapshot.mode) << snapshot.displayName;
    stream << snapshot.selectedDefinitionName << snapshot.automaticallyDetectedName
           << static_cast<qint32>(snapshot.languageSource)
           << snapshot.hasLastNonMarkdownSelection
           << snapshot.lastNonMarkdownDefinitionName
           << static_cast<qint32>(snapshot.lastNonMarkdownSource);
    stream << static_cast<qint32>(snapshot.encoding);
    return stream.status() == QDataStream::Ok && file.commit();
}

QList<RecoverySnapshot> RecoveryManager::readAll()
{
    QList<RecoverySnapshot> result;
    const QDir dir(recoveryDirectory());
    for (const QFileInfo& info : dir.entryInfoList({QStringLiteral("*.recovery")}, QDir::Files)) {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        QDataStream stream(&file);
        quint32 magic = 0;
        quint16 version = 0;
        RecoverySnapshot snapshot;
        qint32 mode = 0;
        stream >> magic >> version >> snapshot.path >> snapshot.content
               >> snapshot.lineEnding >> mode >> snapshot.displayName;
        if (stream.status() != QDataStream::Ok || magic != kMagic
            || version < 1 || version > kVersion)
            continue;

        snapshot.key = info.completeBaseName();
        snapshot.mode = static_cast<EditorMode>(mode);
        if (version >= 2) {
            qint32 languageSource = 0;
            qint32 lastNonMarkdownSource = 0;
            stream >> snapshot.selectedDefinitionName >> snapshot.automaticallyDetectedName
                   >> languageSource >> snapshot.hasLastNonMarkdownSelection
                   >> snapshot.lastNonMarkdownDefinitionName >> lastNonMarkdownSource;
            if (stream.status() != QDataStream::Ok)
                continue;
            snapshot.languageSource = static_cast<LanguageSelectionSource>(languageSource);
            snapshot.lastNonMarkdownSource = static_cast<LanguageSelectionSource>(lastNonMarkdownSource);
            snapshot.hasLanguageState = true;
        }
        if (version >= 3) {
            qint32 encoding = 0;
            stream >> encoding;
            if (stream.status() != QDataStream::Ok)
                continue;
            snapshot.encoding = static_cast<TextEncoding>(encoding);
        }
        result.append(snapshot);
    }
    return result;
}

void RecoveryManager::remove(const QString& key)
{
    if (!key.isEmpty())
        QFile::remove(recoveryDirectory() + QLatin1Char('/') + key + QStringLiteral(".recovery"));
}

void RecoveryManager::clear()
{
    const QDir dir(recoveryDirectory());
    for (const QFileInfo& info : dir.entryInfoList({QStringLiteral("*.recovery")}, QDir::Files))
        QFile::remove(info.absoluteFilePath());
}
