#include "diagnostics/diagnostics_service.hpp"

#include "clash/mode_controller.hpp"
#include "diagnostics/diagnostics_parsing.hpp"

#include <QDateTime>
#include <QFileInfo>
#include <QHostAddress>
#include <QStringList>

#include <maxminddb.h>

namespace tunlet::diagnostics {
namespace {

QString formatRate(double valueKbps) {
    if (valueKbps < 0.0) {
        return "-";
    }
    if (valueKbps >= 1000.0) {
        return QString("%1 Mbps").arg(valueKbps / 1000.0, 0, 'f', 1);
    }
    return QString("%1 kbps").arg(valueKbps, 0, 'f', valueKbps >= 100.0 ? 0 : 1);
}

QString formatBytes(double bytes) {
    if (bytes < 0.0) {
        return "n/a";
    }

    static const char *suffixes[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    int suffixIndex = 0;
    double value = bytes;
    while (value >= 1024.0 && suffixIndex < 4) {
        value /= 1024.0;
        ++suffixIndex;
    }

    const int precision = value >= 100.0 || suffixIndex == 0 ? 0 : 1;
    return QString("%1 %2").arg(value, 0, 'f', precision).arg(QString::fromUtf8(suffixes[suffixIndex]));
}

QString commandName(const config::DiagnosticsCommandConfig &command) {
    const QFileInfo info(command.executable);
    return info.fileName().isEmpty() ? command.executable : info.fileName();
}

QString formatRefreshInterval(int refreshIntervalMs) {
    if (refreshIntervalMs > 0 && refreshIntervalMs % 60000 == 0) {
        return QString("%1 min").arg(refreshIntervalMs / 60000);
    }
    if (refreshIntervalMs > 0 && refreshIntervalMs % 1000 == 0) {
        return QString("%1 s").arg(refreshIntervalMs / 1000);
    }
    return QString("%1 ms").arg(refreshIntervalMs);
}

QString timingBreakdown(const DiagnosticsSnapshot &snapshot) {
    QStringList parts;
    if (snapshot.delayDnsMs >= 0) {
        parts.push_back(QString("DNS %1 ms").arg(snapshot.delayDnsMs));
    }
    if (snapshot.delayConnectMs >= 0) {
        parts.push_back(QString("Connect %1 ms").arg(snapshot.delayConnectMs));
    }
    if (snapshot.delayTlsMs >= 0) {
        parts.push_back(QString("TLS %1 ms").arg(snapshot.delayTlsMs));
    }
    return parts.isEmpty() ? QString("Delay components unavailable") : parts.join(" · ");
}

QString lookupMmdbString(const MMDB_entry_s *entry,
                         const char *segment1,
                         const char *segment2 = nullptr,
                         const char *segment3 = nullptr,
                         const char *segment4 = nullptr) {
    MMDB_entry_data_s entryData;
    int status = MMDB_get_value(const_cast<MMDB_entry_s *>(entry), &entryData, segment1, segment2, segment3, segment4, nullptr);
    if (status != MMDB_SUCCESS || !entryData.has_data || entryData.type != MMDB_DATA_TYPE_UTF8_STRING) {
        return {};
    }

    return QString::fromUtf8(entryData.utf8_string, entryData.data_size).trimmed();
}

bool isUsableIpAddress(const QString &value) {
    if (value.isEmpty()) {
        return false;
    }

    QHostAddress address;
    return address.setAddress(value.trimmed());
}

QString locationLookupError(const QString &ipAddress, const QString &reason) {
    return QString("Location lookup failed for %1: %2").arg(ipAddress, reason);
}

QString describeProcessFailure(const QString &probeName, QProcess *process, const QByteArray &stderrOutput, int timeoutMs) {
    if (process->property("timedOut").toBool()) {
        return QString("%1 timed out after %2 ms").arg(probeName).arg(timeoutMs);
    }
    if (process->error() == QProcess::FailedToStart) {
        return QString("%1 failed to start: %2").arg(probeName, process->errorString());
    }
    if (process->exitStatus() != QProcess::NormalExit) {
        return QString("%1 crashed").arg(probeName);
    }

    const QString stderrText = QString::fromUtf8(stderrOutput).trimmed();
    if (process->exitCode() != 0) {
        if (!stderrText.isEmpty()) {
            return QString("%1 failed: %2").arg(probeName, stderrText);
        }
        return QString("%1 failed with exit code %2").arg(probeName).arg(process->exitCode());
    }

    return stderrText.isEmpty() ? QString("%1 failed").arg(probeName)
                                : QString("%1 failed: %2").arg(probeName, stderrText);
}

}  // namespace

DiagnosticsService::DiagnosticsService(const config::AppConfig &config, clash::ClashApiClient *client, QObject *parent)
    : QObject(parent), m_config(config), m_client(client) {
    connect(m_client, &clash::ClashApiClient::healthCheckFinished, this, &DiagnosticsService::handleHealthResult);
    connect(m_client, &clash::ClashApiClient::trafficFinished, this, &DiagnosticsService::handleTrafficResult);
    connect(&m_timer, &QTimer::timeout, this, &DiagnosticsService::refreshNow);
    updateConfigurationSnapshot();
}

void DiagnosticsService::start() {
    updateConfigurationSnapshot();
    if (!m_config.diagnostics.enabled) {
        resetConnectionSnapshot("Diagnostics disabled");
        return;
    }

    m_timer.start(m_config.diagnostics.refreshIntervalMs);
    refreshNow();
}

void DiagnosticsService::refreshNow() {
    if (!m_config.diagnostics.enabled) {
        return;
    }

    m_client->checkHealth(m_config.clashApi);
    m_client->fetchTraffic(m_config.clashApi);

    ++m_probeGeneration;
    const quint64 generation = m_probeGeneration;
    m_snapshot.externalDetail = "Refreshing connection diagnostics";
    emitSnapshotUpdate();

    startIpv4Probe(generation);
    startTimingProbe(generation);
    startDnsProbe(generation);
}

void DiagnosticsService::updateConfig(const config::AppConfig &config) {
    const bool wasEnabled = m_config.diagnostics.enabled;
    m_config = config;
    m_lastObservedModeValue.clear();
    updateConfigurationSnapshot();

    if (!m_config.diagnostics.enabled) {
        m_timer.stop();
        abortProbe(m_ipv4Process);
        abortProbe(m_timingProcess);
        abortProbe(m_dnsProcess);
        resetConnectionSnapshot("Diagnostics disabled");
        return;
    }

    m_timer.start(m_config.diagnostics.refreshIntervalMs);
    if (!wasEnabled) {
        refreshNow();
    } else {
        emitSnapshotUpdate();
    }
}

DiagnosticsSnapshot DiagnosticsService::snapshot() const {
    return m_snapshot;
}

void DiagnosticsService::observeModeStatus(const tunlet::clash::ModeStatus &status) {
    if (status.busy || status.currentModeValue.isEmpty()) {
        return;
    }

    if (m_lastObservedModeValue.isEmpty()) {
        m_lastObservedModeValue = status.currentModeValue;
        return;
    }

    if (QString::compare(m_lastObservedModeValue, status.currentModeValue, Qt::CaseInsensitive) == 0) {
        return;
    }

    m_lastObservedModeValue = status.currentModeValue;
    refreshNow();
}

void DiagnosticsService::handleHealthResult(const clash::HealthCheckResult &result) {
    m_snapshot.apiReachable = result.ok;
    m_snapshot.apiDetail = result.detail;
    emitSnapshotUpdate();
}

void DiagnosticsService::handleTrafficResult(const clash::TrafficResult &result) {
    m_snapshot.trafficAvailable = result.ok;
    if (!result.ok) {
        m_snapshot.trafficSummary = "Unavailable";
        m_snapshot.trafficDetail = result.detail;
    } else {
        m_snapshot.trafficSummary =
            QString("Down %1 | Up %2").arg(formatRate(result.downloadKbps), formatRate(result.uploadKbps));
        if (result.downloadTotalBytes >= 0.0 || result.uploadTotalBytes >= 0.0) {
            m_snapshot.trafficDetail =
                QString("Total down %1 | Total up %2")
                    .arg(formatBytes(result.downloadTotalBytes), formatBytes(result.uploadTotalBytes));
        } else {
            m_snapshot.trafficDetail = "Kernel reports current throughput only.";
        }
    }

    emitSnapshotUpdate();
}

void DiagnosticsService::updateConfigurationSnapshot() {
    const auto &diagnostics = m_config.diagnostics;
    if (!diagnostics.enabled) {
        m_snapshot.configurationSummary = "Disabled";
        m_snapshot.configurationDetail = "Connection diagnostics are disabled in config.";
        return;
    }

    m_snapshot.configurationSummary =
        QString("IP %1 · Delay %2 · DNS %3")
            .arg(commandName(diagnostics.connection.ipv4),
                 commandName(diagnostics.connection.timing),
                 commandName(diagnostics.connection.dns));
    m_snapshot.configurationDetail =
        QString("Geo DB: %1 · refresh %2 · timeout %3 ms")
            .arg(diagnostics.connection.location.databasePath,
                 formatRefreshInterval(diagnostics.refreshIntervalMs),
                 QString::number(diagnostics.requestTimeoutMs));
}

void DiagnosticsService::resetConnectionSnapshot(const QString &reason) {
    m_snapshot.publicIp.clear();
    m_snapshot.publicIpDetail = reason;
    m_snapshot.location.clear();
    m_snapshot.locationDetail = reason;
    m_snapshot.delayDnsMs = -1;
    m_snapshot.delayConnectMs = -1;
    m_snapshot.delayTlsMs = -1;
    m_snapshot.delayTotalMs = -1;
    m_snapshot.delayDetail = reason;
    m_snapshot.dnsSummary = "Unavailable";
    m_snapshot.dnsDetail = reason;
    m_snapshot.externalDetail = reason;
    emitSnapshotUpdate();
}

void DiagnosticsService::abortProbe(QPointer<QProcess> &process) {
    if (!process) {
        return;
    }

    disconnect(process, nullptr, this, nullptr);
    if (process->state() != QProcess::NotRunning) {
        process->kill();
        process->waitForFinished(100);
    }
    process->deleteLater();
    process = nullptr;
}

void DiagnosticsService::startIpv4Probe(quint64 generation) {
    abortProbe(m_ipv4Process);

    auto *process = new QProcess(this);
    process->setProgram(m_config.diagnostics.connection.ipv4.executable);
    process->setArguments(m_config.diagnostics.connection.ipv4.args);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    process->setProperty("generation", QVariant::fromValue<qulonglong>(generation));
    m_ipv4Process = process;

    QTimer::singleShot(m_config.diagnostics.requestTimeoutMs, process, [process]() {
        if (process->state() != QProcess::NotRunning) {
            process->setProperty("timedOut", true);
            process->kill();
        }
    });

    connect(process, &QProcess::errorOccurred, this, [this, process, generation](QProcess::ProcessError error) {
        if (generation != m_probeGeneration || error != QProcess::FailedToStart || process->property("handledError").toBool()) {
            return;
        }
        process->setProperty("handledError", true);
        const QString failure = QString("Public IP probe failed to start: %1").arg(process->errorString());
        m_snapshot.publicIpDetail = failure;
        m_snapshot.externalDetail = failure;
        if (m_snapshot.publicIp.isEmpty()) {
            m_snapshot.location.clear();
            m_snapshot.locationDetail = "Waiting for a usable public IP before resolving location.";
        }
        emitSnapshotUpdate();
        process->deleteLater();
    });

    connect(process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            [this, process, generation](int, QProcess::ExitStatus) {
                const QByteArray stdOut = process->readAllStandardOutput();
                const QByteArray stdErr = process->readAllStandardError();
                if (process->property("handledError").toBool()) {
                    process->deleteLater();
                    return;
                }
                if (generation != m_probeGeneration) {
                    process->deleteLater();
                    return;
                }

                const QString failure = describeProcessFailure(
                    "Public IP probe", process, stdErr, m_config.diagnostics.requestTimeoutMs);
                if (process->exitCode() == 0 && process->exitStatus() == QProcess::NormalExit &&
                    !process->property("timedOut").toBool()) {
                    QString parseFailure;
                    const QString ip = parsePublicIpOutput(stdOut, &parseFailure);
                    if (!ip.isEmpty()) {
                        m_snapshot.publicIp = ip;
                        m_snapshot.publicIpDetail = QString("Resolved via %1").arg(commandName(m_config.diagnostics.connection.ipv4));
                        m_snapshot.externalDetail = QString("Public IP updated: %1").arg(ip);
                        updateLocationFromPublicIp();
                        emitSnapshotUpdate();
                        process->deleteLater();
                        return;
                    }
                    m_snapshot.publicIpDetail = QString("Public IP parse failed: %1").arg(parseFailure);
                    m_snapshot.externalDetail = m_snapshot.publicIpDetail;
                    if (m_snapshot.publicIp.isEmpty()) {
                        m_snapshot.location.clear();
                        m_snapshot.locationDetail = "Waiting for a usable public IP before resolving location.";
                    }
                } else {
                    m_snapshot.publicIpDetail = failure;
                    m_snapshot.externalDetail = failure;
                    if (m_snapshot.publicIp.isEmpty()) {
                        m_snapshot.location.clear();
                        m_snapshot.locationDetail = "Waiting for a usable public IP before resolving location.";
                    }
                }

                emitSnapshotUpdate();
                process->deleteLater();
            });

    process->start();
}

void DiagnosticsService::startTimingProbe(quint64 generation) {
    abortProbe(m_timingProcess);

    auto *process = new QProcess(this);
    process->setProgram(m_config.diagnostics.connection.timing.executable);
    process->setArguments(m_config.diagnostics.connection.timing.args);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    process->setProperty("generation", QVariant::fromValue<qulonglong>(generation));
    m_timingProcess = process;

    QTimer::singleShot(m_config.diagnostics.requestTimeoutMs, process, [process]() {
        if (process->state() != QProcess::NotRunning) {
            process->setProperty("timedOut", true);
            process->kill();
        }
    });

    connect(process, &QProcess::errorOccurred, this, [this, process, generation](QProcess::ProcessError error) {
        if (generation != m_probeGeneration || error != QProcess::FailedToStart || process->property("handledError").toBool()) {
            return;
        }
        process->setProperty("handledError", true);
        const QString failure = QString("Delay probe failed to start: %1").arg(process->errorString());
        m_snapshot.delayDetail = failure;
        m_snapshot.externalDetail = failure;
        emitSnapshotUpdate();
        process->deleteLater();
    });

    connect(process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            [this, process, generation](int, QProcess::ExitStatus) {
                const QByteArray stdOut = process->readAllStandardOutput();
                const QByteArray stdErr = process->readAllStandardError();
                if (process->property("handledError").toBool()) {
                    process->deleteLater();
                    return;
                }
                if (generation != m_probeGeneration) {
                    process->deleteLater();
                    return;
                }

                if (process->exitCode() == 0 && process->exitStatus() == QProcess::NormalExit &&
                    !process->property("timedOut").toBool()) {
                    QString parseFailure;
                    const ParsedTimingResult parsed = parseTimingOutput(stdOut, &parseFailure);
                    if (parsed.ok) {
                        m_snapshot.delayDnsMs = parsed.dnsMs;
                        m_snapshot.delayConnectMs = parsed.connectMs;
                        m_snapshot.delayTlsMs = parsed.tlsMs;
                        m_snapshot.delayTotalMs = parsed.totalMs;
                        m_snapshot.delayDetail = timingBreakdown(m_snapshot);
                        m_snapshot.externalDetail = QString("Delay updated: %1 ms").arg(parsed.totalMs);
                        emitSnapshotUpdate();
                        process->deleteLater();
                        return;
                    }
                    m_snapshot.delayDetail = QString("Delay parse failed: %1").arg(parseFailure);
                    m_snapshot.externalDetail = m_snapshot.delayDetail;
                } else {
                    const QString failure = describeProcessFailure(
                        "Delay probe", process, stdErr, m_config.diagnostics.requestTimeoutMs);
                    m_snapshot.delayDetail = failure;
                    m_snapshot.externalDetail = failure;
                }

                emitSnapshotUpdate();
                process->deleteLater();
            });

    process->start();
}

void DiagnosticsService::startDnsProbe(quint64 generation) {
    abortProbe(m_dnsProcess);

    auto *process = new QProcess(this);
    process->setProgram(m_config.diagnostics.connection.dns.executable);
    process->setArguments(m_config.diagnostics.connection.dns.args);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    process->setProperty("generation", QVariant::fromValue<qulonglong>(generation));
    m_dnsProcess = process;

    QTimer::singleShot(m_config.diagnostics.requestTimeoutMs, process, [process]() {
        if (process->state() != QProcess::NotRunning) {
            process->setProperty("timedOut", true);
            process->kill();
        }
    });

    connect(process, &QProcess::errorOccurred, this, [this, process, generation](QProcess::ProcessError error) {
        if (generation != m_probeGeneration || error != QProcess::FailedToStart || process->property("handledError").toBool()) {
            return;
        }
        process->setProperty("handledError", true);
        const QString failure = QString("DNS probe failed to start: %1").arg(process->errorString());
        m_snapshot.dnsDetail = failure;
        m_snapshot.externalDetail = failure;
        emitSnapshotUpdate();
        process->deleteLater();
    });

    connect(process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            [this, process, generation](int, QProcess::ExitStatus) {
                const QByteArray stdOut = process->readAllStandardOutput();
                const QByteArray stdErr = process->readAllStandardError();
                if (process->property("handledError").toBool()) {
                    process->deleteLater();
                    return;
                }
                if (generation != m_probeGeneration) {
                    process->deleteLater();
                    return;
                }

                if (process->exitCode() == 0 && process->exitStatus() == QProcess::NormalExit &&
                    !process->property("timedOut").toBool()) {
                    QString parseFailure;
                    const QString dnsSummary = parseDnsOutput(stdOut, &parseFailure);
                    if (!dnsSummary.isEmpty()) {
                        m_snapshot.dnsSummary = dnsSummary;
                        m_snapshot.dnsDetail = QString("Resolved via %1").arg(commandName(m_config.diagnostics.connection.dns));
                        m_snapshot.externalDetail = "DNS updated";
                        emitSnapshotUpdate();
                        process->deleteLater();
                        return;
                    }
                    m_snapshot.dnsDetail = QString("DNS parse failed: %1").arg(parseFailure);
                    m_snapshot.externalDetail = m_snapshot.dnsDetail;
                } else {
                    const QString failure = describeProcessFailure(
                        "DNS probe", process, stdErr, m_config.diagnostics.requestTimeoutMs);
                    m_snapshot.dnsDetail = failure;
                    m_snapshot.externalDetail = failure;
                }

                emitSnapshotUpdate();
                process->deleteLater();
            });

    process->start();
}

void DiagnosticsService::updateLocationFromPublicIp() {
    if (!m_config.diagnostics.connection.location.enabled) {
        m_snapshot.location.clear();
        m_snapshot.locationDetail = "Location lookup disabled in config.";
        return;
    }

    if (!isUsableIpAddress(m_snapshot.publicIp)) {
        m_snapshot.location.clear();
        m_snapshot.locationDetail = "Waiting for a usable public IP before resolving location.";
        return;
    }

    const QString dbPath = m_config.diagnostics.connection.location.databasePath;
    QFileInfo dbInfo(dbPath);
    if (!dbInfo.exists() || !dbInfo.isFile()) {
        m_snapshot.location.clear();
        m_snapshot.locationDetail = locationLookupError(m_snapshot.publicIp, QString("Geo DB not found at %1").arg(dbPath));
        return;
    }

    MMDB_s mmdb;
    const QByteArray dbPathBytes = dbPath.toUtf8();
    const int openStatus = MMDB_open(dbPathBytes.constData(), MMDB_MODE_MMAP, &mmdb);
    if (openStatus != MMDB_SUCCESS) {
        m_snapshot.location.clear();
        m_snapshot.locationDetail = locationLookupError(
            m_snapshot.publicIp,
            QString("failed to open %1").arg(QString::fromUtf8(MMDB_strerror(openStatus))));
        return;
    }

    const QByteArray ipBytes = m_snapshot.publicIp.toUtf8();
    int gaiError = 0;
    int mmdbError = 0;
    const MMDB_lookup_result_s lookup = MMDB_lookup_string(&mmdb, ipBytes.constData(), &gaiError, &mmdbError);
    if (gaiError != 0) {
        m_snapshot.location.clear();
        m_snapshot.locationDetail = locationLookupError(m_snapshot.publicIp, QString("gai error %1").arg(gaiError));
        MMDB_close(&mmdb);
        return;
    }
    if (mmdbError != MMDB_SUCCESS) {
        m_snapshot.location.clear();
        m_snapshot.locationDetail = locationLookupError(
            m_snapshot.publicIp,
            QString::fromUtf8(MMDB_strerror(mmdbError)));
        MMDB_close(&mmdb);
        return;
    }
    if (!lookup.found_entry) {
        m_snapshot.location.clear();
        m_snapshot.locationDetail = locationLookupError(m_snapshot.publicIp, "address not found in Geo DB");
        MMDB_close(&mmdb);
        return;
    }

    QStringList parts;
    const QString city = lookupMmdbString(&lookup.entry, "city", "names", "en");
    const QString region = lookupMmdbString(&lookup.entry, "subdivisions", "0", "names", "en");
    const QString country = lookupMmdbString(&lookup.entry, "country", "names", "en");
    if (!city.isEmpty()) {
        parts.push_back(city);
    }
    if (!region.isEmpty() && region != city) {
        parts.push_back(region);
    }
    if (!country.isEmpty()) {
        parts.push_back(country);
    }

    m_snapshot.location = parts.isEmpty() ? "Unknown location" : parts.join(", ");
    m_snapshot.locationDetail = QString("Lookup IP: %1 · Geo DB: %2").arg(m_snapshot.publicIp, dbPath);
    MMDB_close(&mmdb);
}

void DiagnosticsService::emitSnapshotUpdate() {
    m_snapshot.lastUpdated = QDateTime::currentDateTime();
    emit diagnosticsUpdated(m_snapshot);
}

}  // namespace tunlet::diagnostics
