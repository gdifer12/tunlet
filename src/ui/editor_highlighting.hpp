#pragma once

#include <QSyntaxHighlighter>

namespace tunlet::ui {

class JsonSyntaxHighlighter : public QSyntaxHighlighter {
public:
    explicit JsonSyntaxHighlighter(QTextDocument *document);

protected:
    void highlightBlock(const QString &text) override;
};

class YamlSyntaxHighlighter : public QSyntaxHighlighter {
public:
    explicit YamlSyntaxHighlighter(QTextDocument *document);

protected:
    void highlightBlock(const QString &text) override;
};

}  // namespace tunlet::ui
