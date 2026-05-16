#include "ui/editor_highlighting.hpp"

#include <QColor>
#include <QFont>
#include <QRegularExpression>
#include <QTextCharFormat>

namespace tunlet::ui {
namespace {

QTextCharFormat buildFormat(const QColor &foreground, bool bold = false, bool italic = false) {
    QTextCharFormat format;
    format.setForeground(foreground);
    if (bold) {
        format.setFontWeight(QFont::DemiBold);
    }
    format.setFontItalic(italic);
    return format;
}

const QTextCharFormat &jsonKeyFormat() {
    static const QTextCharFormat format = buildFormat(QColor("#8db9ff"), true);
    return format;
}

const QTextCharFormat &jsonStringFormat() {
    static const QTextCharFormat format = buildFormat(QColor("#9ad9a3"));
    return format;
}

const QTextCharFormat &jsonNumberFormat() {
    static const QTextCharFormat format = buildFormat(QColor("#f3c87a"));
    return format;
}

const QTextCharFormat &jsonLiteralFormat() {
    static const QTextCharFormat format = buildFormat(QColor("#d8a7ff"), true);
    return format;
}

const QTextCharFormat &jsonPunctuationFormat() {
    static const QTextCharFormat format = buildFormat(QColor("#7f8ea3"));
    return format;
}

const QTextCharFormat &yamlKeyFormat() {
    static const QTextCharFormat format = buildFormat(QColor("#8db9ff"), true);
    return format;
}

const QTextCharFormat &yamlStringFormat() {
    static const QTextCharFormat format = buildFormat(QColor("#9ad9a3"));
    return format;
}

const QTextCharFormat &yamlNumberFormat() {
    static const QTextCharFormat format = buildFormat(QColor("#f3c87a"));
    return format;
}

const QTextCharFormat &yamlLiteralFormat() {
    static const QTextCharFormat format = buildFormat(QColor("#d8a7ff"), true);
    return format;
}

const QTextCharFormat &yamlCommentFormat() {
    static const QTextCharFormat format = buildFormat(QColor("#697687"), false, true);
    return format;
}

const QTextCharFormat &yamlMarkerFormat() {
    static const QTextCharFormat format = buildFormat(QColor("#7f8ea3"), true);
    return format;
}

int findYamlCommentStart(const QString &text) {
    bool inSingleQuotes = false;
    bool inDoubleQuotes = false;

    for (int index = 0; index < text.size(); ++index) {
        const QChar character = text.at(index);
        if (character == '\'' && !inDoubleQuotes) {
            inSingleQuotes = !inSingleQuotes;
            continue;
        }
        if (character == '"' && !inSingleQuotes && (index == 0 || text.at(index - 1) != '\\')) {
            inDoubleQuotes = !inDoubleQuotes;
            continue;
        }
        if (character == '#' && !inSingleQuotes && !inDoubleQuotes) {
            return index;
        }
    }

    return -1;
}

}  // namespace

JsonSyntaxHighlighter::JsonSyntaxHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document) {}

void JsonSyntaxHighlighter::highlightBlock(const QString &text) {
    static const QRegularExpression keyPattern(R"("([^"\\]|\\.)*"(?=\s*:))");
    static const QRegularExpression stringPattern(R"("([^"\\]|\\.)*")");
    static const QRegularExpression numberPattern(R"(\b-?(0|[1-9]\d*)(\.\d+)?([eE][+-]?\d+)?\b)");
    static const QRegularExpression literalPattern(R"(\b(true|false|null)\b)");
    static const QRegularExpression punctuationPattern(R"([{}\[\]:,])");

    auto highlightPattern = [this, &text](const QRegularExpression &pattern, const QTextCharFormat &format, int captureGroup = 0) {
        auto iterator = pattern.globalMatch(text);
        while (iterator.hasNext()) {
            const auto match = iterator.next();
            const int start = match.capturedStart(captureGroup);
            const int length = match.capturedLength(captureGroup);
            if (start >= 0 && length > 0) {
                setFormat(start, length, format);
            }
        }
    };

    highlightPattern(stringPattern, jsonStringFormat());
    highlightPattern(keyPattern, jsonKeyFormat());
    highlightPattern(numberPattern, jsonNumberFormat());
    highlightPattern(literalPattern, jsonLiteralFormat());
    highlightPattern(punctuationPattern, jsonPunctuationFormat());
}

YamlSyntaxHighlighter::YamlSyntaxHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document) {}

void YamlSyntaxHighlighter::highlightBlock(const QString &text) {
    static const QRegularExpression quotedStringPattern(R"(\"([^\"\\]|\\.)*\"|'[^']*')");
    static const QRegularExpression keyPattern(R"(^(\s*)([^:#\-\[\]\{\},][^:]*)(?=\s*:))");
    static const QRegularExpression numberPattern(R"((?<![\w.-])[-+]?(0|[1-9]\d*)(\.\d+)?(?![\w.-]))");
    static const QRegularExpression literalPattern(R"(\b(true|false|null|yes|no|on|off)\b)", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression listMarkerPattern(R"(^\s*-\s)");
    static const QRegularExpression anchorPattern(R"([&*!][A-Za-z0-9._-]+)");

    auto highlightPattern = [this, &text](const QRegularExpression &pattern, const QTextCharFormat &format, int captureGroup = 0) {
        auto iterator = pattern.globalMatch(text);
        while (iterator.hasNext()) {
            const auto match = iterator.next();
            const int start = match.capturedStart(captureGroup);
            const int length = match.capturedLength(captureGroup);
            if (start >= 0 && length > 0) {
                setFormat(start, length, format);
            }
        }
    };

    highlightPattern(quotedStringPattern, yamlStringFormat());
    highlightPattern(keyPattern, yamlKeyFormat(), 2);
    highlightPattern(numberPattern, yamlNumberFormat());
    highlightPattern(literalPattern, yamlLiteralFormat());
    highlightPattern(listMarkerPattern, yamlMarkerFormat());
    highlightPattern(anchorPattern, yamlMarkerFormat());

    const int commentStart = findYamlCommentStart(text);
    if (commentStart >= 0) {
        setFormat(commentStart, text.size() - commentStart, yamlCommentFormat());
    }
}

}  // namespace tunlet::ui
