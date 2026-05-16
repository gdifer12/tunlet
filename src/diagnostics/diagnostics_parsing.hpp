#pragma once

#include <QByteArray>
#include <QString>

namespace tunlet::diagnostics {

struct ParsedTimingResult {
    bool ok = false;
    qint64 dnsMs = -1;
    qint64 connectMs = -1;
    qint64 tlsMs = -1;
    qint64 totalMs = -1;
};

QString parsePublicIpOutput(const QByteArray &output, QString *failureReason = nullptr);
ParsedTimingResult parseTimingOutput(const QByteArray &output, QString *failureReason = nullptr);
QString parseDnsOutput(const QByteArray &output, QString *failureReason = nullptr);

}  // namespace tunlet::diagnostics
