#pragma once

#include "config/app_config.hpp"

#include <QDateTime>
#include <QMap>
#include <QObject>
#include <QString>

class QFile;

namespace tunlet::logging {

using LogContext = QMap<QString, QString>;

struct LogEvent {
    QDateTime timestampUtc;
    config::LoggingLevel level = config::LoggingLevel::Info;
    QString source;
    QString summary;
    QString detail;
    LogContext context;
};

struct LoggingStatus {
    bool enabled = false;
    config::LoggingLevel level = config::LoggingLevel::Info;
    QString textPath;
    QString jsonlPath;
    bool textSinkActive = false;
    bool jsonlSinkActive = false;
    QString lastError;
};

QString loggingLevelToString(config::LoggingLevel level);

class LoggingService : public QObject {
    Q_OBJECT

public:
    explicit LoggingService(const config::AppConfig &config, QObject *parent = nullptr);
    ~LoggingService() override;

    void updateConfig(const config::AppConfig &config);
    void logInfo(const QString &source,
                 const QString &summary,
                 const QString &detail = {},
                 const LogContext &context = {});
    void logWarning(const QString &source,
                    const QString &summary,
                    const QString &detail = {},
                    const LogContext &context = {});
    void logError(const QString &source,
                  const QString &summary,
                  const QString &detail = {},
                  const LogContext &context = {});

    LoggingStatus status() const;

signals:
    void statusChanged(const tunlet::logging::LoggingStatus &status);

private:
    bool shouldWrite(config::LoggingLevel level) const;
    void log(config::LoggingLevel level,
             const QString &source,
             const QString &summary,
             const QString &detail,
             const LogContext &context);
    void reconfigure();
    bool openSink(QFile *file, const QString &path, const QString &sinkName, bool *active, QString *errorOut);
    void closeSinks();
    void recordSinkFailure(const QString &sinkName, const QString &detail, bool deactivateTextSink, bool deactivateJsonlSink);
    void publishStatus();
    QString formatTextLine(const LogEvent &event) const;
    QByteArray formatJsonLine(const LogEvent &event) const;

    config::LoggingConfig m_config;
    LoggingStatus m_status;
    QFile *m_textFile = nullptr;
    QFile *m_jsonlFile = nullptr;
};

}  // namespace tunlet::logging

Q_DECLARE_METATYPE(tunlet::logging::LoggingStatus)
