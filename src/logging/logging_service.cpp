#include "logging/logging_service.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace tunlet::logging {
namespace {

QString normalizedLevelText(config::LoggingLevel level) {
    switch (level) {
    case config::LoggingLevel::Error:
        return "ERROR";
    case config::LoggingLevel::Warning:
        return "WARNING";
    case config::LoggingLevel::Info:
    default:
        return "INFO";
    }
}

QString escapeInlineText(QString text) {
    text.replace('\\', "\\\\");
    text.replace('\r', "\\r");
    text.replace('\n', "\\n");
    text.replace('"', "\\\"");
    return text;
}

QString formatContextValue(const QString &value) {
    if (value.isEmpty()) {
        return "\"\"";
    }

    bool needsQuotes = false;
    for (const QChar ch : value) {
        if (ch.isSpace() || ch == '"' || ch == '|' || ch == '=') {
            needsQuotes = true;
            break;
        }
    }

    const QString escaped = escapeInlineText(value);
    return needsQuotes ? QString("\"%1\"").arg(escaped) : escaped;
}

bool statusEquals(const LoggingStatus &lhs, const LoggingStatus &rhs) {
    return lhs.enabled == rhs.enabled &&
           lhs.level == rhs.level &&
           lhs.textPath == rhs.textPath &&
           lhs.jsonlPath == rhs.jsonlPath &&
           lhs.rotationEnabled == rhs.rotationEnabled &&
           lhs.rotationMaxFileBytes == rhs.rotationMaxFileBytes &&
           lhs.rotationKeepFiles == rhs.rotationKeepFiles &&
           lhs.textSinkActive == rhs.textSinkActive &&
           lhs.jsonlSinkActive == rhs.jsonlSinkActive &&
           lhs.lastError == rhs.lastError;
}

QString utcTimestampText(const QDateTime &time) {
    return time.toUTC().toString("yyyy-MM-ddTHH:mm:ss.zzz'Z'");
}

QString archivePath(const QString &basePath, int index) {
    return QString("%1.%2").arg(basePath).arg(index);
}

}  // namespace

QString loggingLevelToString(config::LoggingLevel level) {
    switch (level) {
    case config::LoggingLevel::Error:
        return "error";
    case config::LoggingLevel::Warning:
        return "warning";
    case config::LoggingLevel::Info:
    default:
        return "info";
    }
}

LoggingService::LoggingService(const config::AppConfig &config, QObject *parent)
    : QObject(parent),
      m_textFile(new QFile(this)),
      m_jsonlFile(new QFile(this)) {
    updateConfig(config);
}

LoggingService::~LoggingService() {
    closeSinks();
}

void LoggingService::updateConfig(const config::AppConfig &config) {
    m_config = config.logging;
    reconfigure();
}

void LoggingService::logInfo(const QString &source, const QString &summary, const QString &detail, const LogContext &context) {
    log(config::LoggingLevel::Info, source, summary, detail, context);
}

void LoggingService::logWarning(const QString &source, const QString &summary, const QString &detail, const LogContext &context) {
    log(config::LoggingLevel::Warning, source, summary, detail, context);
}

void LoggingService::logError(const QString &source, const QString &summary, const QString &detail, const LogContext &context) {
    log(config::LoggingLevel::Error, source, summary, detail, context);
}

LoggingStatus LoggingService::status() const {
    return m_status;
}

bool LoggingService::shouldWrite(config::LoggingLevel level) const {
    return static_cast<int>(level) >= static_cast<int>(m_config.level);
}

void LoggingService::log(config::LoggingLevel level,
                         const QString &source,
                         const QString &summary,
                         const QString &detail,
                         const LogContext &context) {
    if (!m_config.enabled || !shouldWrite(level)) {
        return;
    }

    if (!m_status.textSinkActive && !m_status.jsonlSinkActive) {
        return;
    }

    LogEvent event;
    event.timestampUtc = QDateTime::currentDateTimeUtc();
    event.level = level;
    event.source = source.trimmed().isEmpty() ? QString("app") : source.trimmed();
    event.summary = summary.trimmed().isEmpty() ? QString("No summary") : summary.trimmed();
    event.detail = detail.trimmed();
    event.context = context;

    if (m_status.textSinkActive) {
        const QByteArray line = formatTextLine(event).toUtf8() + '\n';
        QString rotationError;
        if (!ensureSinkReadyForWrite(
                m_textFile, m_status.textPath, "text", line.size(), &m_status.textSinkActive, &rotationError)) {
            recordSinkFailure("text",
                              rotationError,
                              /*deactivateTextSink=*/true,
                              /*deactivateJsonlSink=*/false);
        } else if (m_textFile->write(line) != line.size() || !m_textFile->flush()) {
            recordSinkFailure("text",
                              QString("Failed to write text log %1: %2")
                                  .arg(m_status.textPath, m_textFile->errorString()),
                              /*deactivateTextSink=*/true,
                              /*deactivateJsonlSink=*/false);
        }
    }

    if (m_status.jsonlSinkActive) {
        const QByteArray line = formatJsonLine(event) + '\n';
        QString rotationError;
        if (!ensureSinkReadyForWrite(
                m_jsonlFile, m_status.jsonlPath, "jsonl", line.size(), &m_status.jsonlSinkActive, &rotationError)) {
            recordSinkFailure("jsonl",
                              rotationError,
                              /*deactivateTextSink=*/false,
                              /*deactivateJsonlSink=*/true);
        } else if (m_jsonlFile->write(line) != line.size() || !m_jsonlFile->flush()) {
            recordSinkFailure("jsonl",
                              QString("Failed to write JSONL log %1: %2")
                                  .arg(m_status.jsonlPath, m_jsonlFile->errorString()),
                              /*deactivateTextSink=*/false,
                              /*deactivateJsonlSink=*/true);
        }
    }
}

void LoggingService::reconfigure() {
    closeSinks();

    LoggingStatus newStatus;
    newStatus.enabled = m_config.enabled;
    newStatus.level = m_config.level;
    newStatus.textPath = m_config.textPath;
    newStatus.jsonlPath = m_config.jsonlPath;
    newStatus.rotationEnabled = m_config.rotation.enabled;
    newStatus.rotationMaxFileBytes = m_config.rotation.maxFileBytes;
    newStatus.rotationKeepFiles = m_config.rotation.keepFiles;

    if (m_config.enabled) {
        QString errorText;
        if (!m_config.textPath.trimmed().isEmpty() &&
            !openSink(m_textFile, m_config.textPath, "text", &newStatus.textSinkActive, &errorText) &&
            !errorText.isEmpty()) {
            newStatus.lastError = errorText;
        }

        QString errorJsonl;
        if (!m_config.jsonlPath.trimmed().isEmpty() &&
            !openSink(m_jsonlFile, m_config.jsonlPath, "jsonl", &newStatus.jsonlSinkActive, &errorJsonl) &&
            !errorJsonl.isEmpty()) {
            newStatus.lastError = newStatus.lastError.isEmpty()
                                      ? errorJsonl
                                      : QString("%1; %2").arg(newStatus.lastError, errorJsonl);
        }
    }

    const LoggingStatus previousStatus = m_status;
    m_status = newStatus;
    if (!statusEquals(previousStatus, m_status)) {
        emit statusChanged(m_status);
    }
}

bool LoggingService::openSink(QFile *file, const QString &path, const QString &sinkName, bool *active, QString *errorOut) {
    if (active) {
        *active = false;
    }
    if (errorOut) {
        errorOut->clear();
    }

    if (!file || path.trimmed().isEmpty()) {
        return false;
    }

    if (!openFileForAppend(file, path, sinkName, errorOut)) {
        return false;
    }

    if (m_config.rotation.enabled && file->size() > m_config.rotation.maxFileBytes) {
        if (file->isOpen()) {
            file->close();
        }
        if (!rotateArchives(path, sinkName, errorOut) || !openFileForAppend(file, path, sinkName, errorOut)) {
            return false;
        }
    }

    if (active) {
        *active = true;
    }
    return true;
}

bool LoggingService::openFileForAppend(QFile *file, const QString &path, const QString &sinkName, QString *errorOut) {
    if (errorOut) {
        errorOut->clear();
    }

    const QFileInfo fileInfo(path);
    QDir dir = fileInfo.dir();
    if (!dir.exists() && !dir.mkpath(".")) {
        if (errorOut) {
            *errorOut = QString("Failed to create directory for %1 log: %2").arg(sinkName, dir.absolutePath());
        }
        return false;
    }

    if (file->isOpen()) {
        file->close();
    }
    file->setFileName(path);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        if (errorOut) {
            *errorOut = QString("Failed to open %1 log %2: %3").arg(sinkName, path, file->errorString());
        }
        return false;
    }
    return true;
}

bool LoggingService::rotateSink(QFile *file, const QString &path, const QString &sinkName, QString *errorOut) {
    if (!rotateArchives(path, sinkName, errorOut)) {
        return false;
    }
    return openFileForAppend(file, path, sinkName, errorOut);
}

bool LoggingService::rotateArchives(const QString &path, const QString &sinkName, QString *errorOut) {
    if (errorOut) {
        errorOut->clear();
    }

    const int keepFiles = m_config.rotation.keepFiles;
    if (keepFiles < 1) {
        return true;
    }

    const QString oldestArchive = archivePath(path, keepFiles);
    if (QFile::exists(oldestArchive) && !QFile::remove(oldestArchive)) {
        if (errorOut) {
            *errorOut = QString("Failed to remove old %1 log archive %2").arg(sinkName, oldestArchive);
        }
        return false;
    }

    for (int index = keepFiles - 1; index >= 1; --index) {
        const QString sourcePath = archivePath(path, index);
        if (!QFile::exists(sourcePath)) {
            continue;
        }

        const QString destinationPath = archivePath(path, index + 1);
        if (QFile::exists(destinationPath) && !QFile::remove(destinationPath)) {
            if (errorOut) {
                *errorOut = QString("Failed to replace %1 log archive %2").arg(sinkName, destinationPath);
            }
            return false;
        }
        if (!QFile::rename(sourcePath, destinationPath)) {
            if (errorOut) {
                *errorOut = QString("Failed to rotate %1 log archive %2 to %3")
                                .arg(sinkName, sourcePath, destinationPath);
            }
            return false;
        }
    }

    if (QFile::exists(path) && !QFile::rename(path, archivePath(path, 1))) {
        if (errorOut) {
            *errorOut = QString("Failed to rotate active %1 log %2").arg(sinkName, path);
        }
        return false;
    }
    return true;
}

bool LoggingService::ensureSinkReadyForWrite(QFile *file,
                                             const QString &path,
                                             const QString &sinkName,
                                             qint64 incomingBytes,
                                             bool *active,
                                             QString *errorOut) {
    if (errorOut) {
        errorOut->clear();
    }
    if (!m_config.rotation.enabled || !file || !file->isOpen()) {
        return true;
    }

    const qint64 currentSize = file->size();
    if (currentSize < 0 || currentSize == 0 || currentSize + incomingBytes <= m_config.rotation.maxFileBytes) {
        return true;
    }

    if (active) {
        *active = false;
    }
    if (file->isOpen()) {
        file->close();
    }

    const bool reopened = rotateSink(file, path, sinkName, errorOut);
    if (active) {
        *active = reopened;
    }
    return reopened;
}

void LoggingService::closeSinks() {
    if (m_textFile && m_textFile->isOpen()) {
        m_textFile->close();
    }
    if (m_jsonlFile && m_jsonlFile->isOpen()) {
        m_jsonlFile->close();
    }
}

void LoggingService::recordSinkFailure(const QString &sinkName,
                                       const QString &detail,
                                       bool deactivateTextSink,
                                       bool deactivateJsonlSink) {
    if (deactivateTextSink && m_textFile && m_textFile->isOpen()) {
        m_textFile->close();
        m_status.textSinkActive = false;
    }
    if (deactivateJsonlSink && m_jsonlFile && m_jsonlFile->isOpen()) {
        m_jsonlFile->close();
        m_status.jsonlSinkActive = false;
    }
    m_status.lastError = sinkName.trimmed().isEmpty() ? detail : QString("%1 sink error: %2").arg(sinkName, detail);
    publishStatus();
}

void LoggingService::publishStatus() {
    emit statusChanged(m_status);
}

QString LoggingService::formatTextLine(const LogEvent &event) const {
    QStringList parts;
    parts << utcTimestampText(event.timestampUtc)
          << normalizedLevelText(event.level)
          << QString("[%1]").arg(event.source)
          << escapeInlineText(event.summary);

    QString line = parts.join(' ');
    if (!event.detail.isEmpty()) {
        line += QString(" | detail=%1").arg(formatContextValue(event.detail));
    }
    for (auto it = event.context.cbegin(); it != event.context.cend(); ++it) {
        line += QString(" | %1=%2").arg(it.key(), formatContextValue(it.value()));
    }
    return line;
}

QByteArray LoggingService::formatJsonLine(const LogEvent &event) const {
    QJsonObject object{
        {"ts", utcTimestampText(event.timestampUtc)},
        {"level", loggingLevelToString(event.level)},
        {"source", event.source},
        {"summary", event.summary},
    };
    if (!event.detail.isEmpty()) {
        object.insert("detail", event.detail);
    }
    if (!event.context.isEmpty()) {
        QJsonObject contextObject;
        for (auto it = event.context.cbegin(); it != event.context.cend(); ++it) {
            contextObject.insert(it.key(), it.value());
        }
        object.insert("context", contextObject);
    }
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

}  // namespace tunlet::logging
