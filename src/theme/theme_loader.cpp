#include "theme/theme_loader.hpp"

#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>

static void ensureThemeResourcesInitialized() {
    static const bool initialized = []() {
        Q_INIT_RESOURCE(resources);
        return true;
    }();
    Q_UNUSED(initialized);
}

namespace tunlet::theme {

namespace {

constexpr auto kBuiltInThemePath = ":/themes/default.theme.json";
constexpr auto kBuiltInTemplatePath = ":/styles/default.qss.in";

bool readUtf8File(const QString &path, const QString &description, QString *content, QString *errorMessage) {
    QFile file(path);
    if (!file.exists()) {
        if (errorMessage) {
            *errorMessage = QString("%1 not found: %2").arg(description, path);
        }
        return false;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QString("failed to open %1: %2").arg(description, file.errorString());
        }
        return false;
    }

    if (content) {
        *content = QString::fromUtf8(file.readAll());
    }
    return true;
}

bool parseThemeObject(const QString &payload, const QString &sourceLabel, QJsonObject *object, QString *errorMessage) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (errorMessage) {
            *errorMessage = QString("failed to parse theme JSON %1: %2").arg(sourceLabel, parseError.errorString());
        }
        return false;
    }
    if (!document.isObject()) {
        if (errorMessage) {
            *errorMessage = QString("theme JSON must be an object: %1").arg(sourceLabel);
        }
        return false;
    }

    if (object) {
        *object = document.object();
    }
    return true;
}

bool loadThemeObject(const QString &path,
                     const QString &description,
                     QJsonObject *themeObject,
                     QString *errorMessage) {
    QString payload;
    if (!readUtf8File(path, description, &payload, errorMessage)) {
        return false;
    }
    return parseThemeObject(payload, path, themeObject, errorMessage);
}

void mergeThemeObjects(QJsonObject *baseObject, const QJsonObject &overrideObject) {
    for (auto it = overrideObject.constBegin(); it != overrideObject.constEnd(); ++it) {
        if (it.value().isObject() && baseObject->value(it.key()).isObject()) {
            QJsonObject nested = baseObject->value(it.key()).toObject();
            mergeThemeObjects(&nested, it.value().toObject());
            baseObject->insert(it.key(), nested);
            continue;
        }
        baseObject->insert(it.key(), it.value());
    }
}

bool resolveThemeToken(const QJsonObject &themeRoot,
                       const QString &tokenPath,
                       QString *resolvedValue,
                       QString *errorMessage) {
    const QStringList parts = tokenPath.split('.', Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        if (errorMessage) {
            *errorMessage = "empty theme token path";
        }
        return false;
    }

    QJsonValue current(themeRoot);
    for (const QString &part : parts) {
        if (!current.isObject()) {
            if (errorMessage) {
                *errorMessage = QString("theme token is not a primitive value: %1").arg(tokenPath);
            }
            return false;
        }
        const QJsonObject object = current.toObject();
        if (!object.contains(part)) {
            if (errorMessage) {
                *errorMessage = QString("missing theme token: %1").arg(tokenPath);
            }
            return false;
        }
        current = object.value(part);
    }

    if (current.isString()) {
        if (resolvedValue) {
            *resolvedValue = current.toString();
        }
        return true;
    }
    if (current.isDouble()) {
        const double value = current.toDouble();
        const qint64 integerValue = static_cast<qint64>(value);
        if (resolvedValue) {
            *resolvedValue = qFuzzyCompare(value + 1.0, static_cast<double>(integerValue) + 1.0)
                                 ? QString::number(integerValue)
                                 : QString::number(value, 'g', 15);
        }
        return true;
    }
    if (current.isBool()) {
        if (resolvedValue) {
            *resolvedValue = current.toBool() ? "true" : "false";
        }
        return true;
    }

    if (errorMessage) {
        *errorMessage = QString("theme token is not a primitive value: %1").arg(tokenPath);
    }
    return false;
}

bool renderTemplate(const QString &templateSource,
                    const QString &templateLabel,
                    const QJsonObject &themeRoot,
                    QString *stylesheet,
                    QString *errorMessage) {
    static const QRegularExpression placeholderPattern(R"(\{\{\s*([A-Za-z0-9_.-]+)\s*\}\})");

    QString rendered = templateSource;
    int offset = 0;
    QRegularExpressionMatch match;
    while ((offset = rendered.indexOf(placeholderPattern, offset, &match)) >= 0) {
        QString tokenValue;
        QString tokenError;
        if (!resolveThemeToken(themeRoot, match.captured(1), &tokenValue, &tokenError)) {
            if (errorMessage) {
                *errorMessage = QString("%1 in %2").arg(tokenError, templateLabel);
            }
            return false;
        }
        rendered.replace(offset, match.capturedLength(0), tokenValue);
        offset += tokenValue.size();
    }

    if (stylesheet) {
        *stylesheet = rendered;
    }
    return true;
}

}  // namespace

bool ThemeLoader::buildStylesheet(const config::ThemeConfig &config, QString *stylesheet, QString *errorMessage) {
    ensureThemeResourcesInitialized();

    if (!stylesheet) {
        if (errorMessage) {
            *errorMessage = "stylesheet output pointer must not be null";
        }
        return false;
    }

    stylesheet->clear();

    QJsonObject themeObject;
    if (!loadThemeObject(kBuiltInThemePath, "built-in theme file", &themeObject, errorMessage)) {
        return false;
    }

    if (!config.themePath.trimmed().isEmpty()) {
        QJsonObject overrideObject;
        if (!loadThemeObject(config.themePath, "theme override file", &overrideObject, errorMessage)) {
            return false;
        }
        mergeThemeObjects(&themeObject, overrideObject);
    }

    const QString templatePath =
        config.templatePath.trimmed().isEmpty() ? QString(kBuiltInTemplatePath) : config.templatePath;
    const QString templateDescription = config.templatePath.trimmed().isEmpty()
                                            ? QString("built-in theme template")
                                            : QString("theme template override file");

    QString templateSource;
    if (!readUtf8File(templatePath, templateDescription, &templateSource, errorMessage)) {
        return false;
    }

    QString renderedStylesheet;
    if (!renderTemplate(templateSource, templatePath, themeObject, &renderedStylesheet, errorMessage)) {
        return false;
    }

    QString finalStylesheet = renderedStylesheet;
    if (!config.qssPath.trimmed().isEmpty()) {
        QString rawOverride;
        if (!readUtf8File(config.qssPath, "QSS override file", &rawOverride, errorMessage)) {
            *stylesheet = finalStylesheet;
            return false;
        }

        if (!finalStylesheet.isEmpty() && !rawOverride.isEmpty()) {
            finalStylesheet += "\n";
        }
        finalStylesheet += rawOverride;
    }

    *stylesheet = finalStylesheet;
    return true;
}

bool ThemeLoader::applyTheme(QApplication &application, const config::ThemeConfig &config, QString *errorMessage) {
    QString stylesheet;
    const bool success = buildStylesheet(config, &stylesheet, errorMessage);
    if (!stylesheet.isEmpty()) {
        application.setStyleSheet(stylesheet);
    }
    return success;
}

}  // namespace tunlet::theme
