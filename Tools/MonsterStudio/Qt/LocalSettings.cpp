#include "LocalSettings.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <Windows.h>
#include <ShlObj.h>
#include <algorithm>
#include <stdexcept>

namespace MonsterStudioQt {
namespace {
constexpr qint64 MaxBytes = 512 * 1024;
constexpr qsizetype MaxRecents = 64;
constexpr qsizetype MaxString = 4096;
constexpr int SchemaVersion = 1;

[[noreturn]] void Fail(const QString& message) {
    throw std::runtime_error(message.toUtf8().constData());
}

QString KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR value = nullptr;
    const HRESULT result = SHGetKnownFolderPath(id, KF_FLAG_DONT_VERIFY, nullptr, &value);
    if (FAILED(result) || !value) {
        if (value) CoTaskMemFree(value);
        Fail(QStringLiteral("Cannot resolve the Windows known folder. Repair the Windows folder configuration and retry."));
    }
    const QString path = QString::fromWCharArray(value);
    CoTaskMemFree(value);
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

void CheckString(const QString& value, bool required = false) {
    if (value.size() > MaxString || value.contains(QChar(u'\0')) || (required && value.isEmpty()))
        Fail(QStringLiteral("GUI settings contain an empty or oversized required value. The original index is preserved; repair or explicitly reset its file."));
}

QString AbsolutePath(const QString& path) {
    CheckString(path, true);
    const QString normalized = QDir::fromNativeSeparators(path);
    if (!QDir::isAbsolutePath(normalized))
        Fail(QStringLiteral("Choose an absolute folder path for GUI settings."));
    return QDir::cleanPath(normalized);
}

// Canonicalization is bounded to explicitly supplied locations, on the worker.
// Existing junctions, symlinks and Windows casing aliases share a navigation key.
QString PathKey(const QString& path) {
    const QString absolute = AbsolutePath(path);
    const QString canonical = QFileInfo(absolute).canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? absolute : canonical).toCaseFolded();
}

QString ReadString(const QJsonObject& object, const char* key, bool required = false) {
    const auto value = object.value(QLatin1String(key));
    if (!value.isString()) Fail(QStringLiteral("GUI settings have invalid fields. The original index is preserved; repair or explicitly reset its file."));
    const QString text = value.toString();
    CheckString(text, required);
    return text;
}

void CheckJsonDepth(const QByteArray& bytes) {
    int depth = 0;
    bool quoted = false, escape = false;
    for (const char c : bytes) {
        if (quoted) {
            if (escape) escape = false;
            else if (c == '\\') escape = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') {
            if (++depth > 8) Fail(QStringLiteral("GUI settings exceed the JSON nesting limit. Preserve and repair or explicitly reset the file."));
        } else if (c == '}' || c == ']') --depth;
    }
}

LocalSettingsSnapshot Read(const QString& file) {
    LocalSettingsSnapshot result;
    if (!QFileInfo::exists(file)) {
        result.projectRoot = LocalSettings::DefaultProjectRoot();
        return result;
    }
    QFile input(file);
    if (!input.open(QIODevice::ReadOnly))
        Fail(QStringLiteral("Cannot read GUI settings: %1. Repair file access and retry.").arg(input.errorString()));
    if (input.size() > MaxBytes || input.size() < 0)
        Fail(QStringLiteral("GUI settings exceed the 512 KiB limit. The original index is preserved; repair or explicitly reset its file."));
    const QByteArray bytes = input.read(MaxBytes + 1);
    if (input.error() != QFileDevice::NoError || bytes.size() > MaxBytes)
        Fail(QStringLiteral("Cannot read bounded GUI settings. The original index is preserved; repair file access and retry."));
    CheckJsonDepth(bytes);
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        Fail(QStringLiteral("GUI settings JSON is damaged. The original index is preserved; repair or explicitly reset its file."));
    const auto object = document.object();
    if (object.value(QStringLiteral("schemaVersion")).toDouble(-1) != SchemaVersion)
        Fail(QStringLiteral("GUI settings schema is unsupported. The original file is preserved; use a compatible application or explicitly reset the file."));
    const QString theme = ReadString(object, "theme", true);
    if (theme == QStringLiteral("system")) result.theme = ThemePreference::System;
    else if (theme == QStringLiteral("light")) result.theme = ThemePreference::Light;
    else if (theme == QStringLiteral("dark")) result.theme = ThemePreference::Dark;
    else Fail(QStringLiteral("GUI settings theme is invalid. Repair or explicitly reset the preserved file."));
    result.projectRoot = AbsolutePath(ReadString(object, "projectRoot", true));
    const auto recents = object.value(QStringLiteral("recentProjects"));
    if (!recents.isArray() || recents.toArray().size() > MaxRecents)
        Fail(QStringLiteral("GUI recent index is invalid or exceeds 64 entries. Repair or explicitly reset the preserved file."));
    for (const auto& value : recents.toArray()) {
        if (!value.isObject()) Fail(QStringLiteral("GUI recent index contains an invalid row. Repair or explicitly reset the preserved file."));
        const auto row = value.toObject();
        RecentProject recent{AbsolutePath(ReadString(row, "directory", true)), ReadString(row, "name"),
            ReadString(row, "projectId", true), ReadString(row, "lastObservedState"), ReadString(row, "lastOpenedUtc", true)};
        const auto time = QDateTime::fromString(recent.lastOpenedUtc, Qt::ISODateWithMs);
        if (!time.isValid() || !recent.lastOpenedUtc.endsWith(QLatin1Char('Z')))
            Fail(QStringLiteral("GUI recent index has an invalid observation time. Repair or explicitly reset the preserved file."));
        result.recentProjects.append(recent);
    }
    return result;
}

void Write(const QString& file, const LocalSettingsSnapshot& settings) {
    const char* theme = settings.theme == ThemePreference::Light ? "light" : settings.theme == ThemePreference::Dark ? "dark" : "system";
    QJsonArray recents;
    for (const auto& row : settings.recentProjects) {
        recents.append(QJsonObject{{"directory", row.directory}, {"name", row.name}, {"projectId", row.projectId},
            {"lastObservedState", row.lastObservedState}, {"lastOpenedUtc", row.lastOpenedUtc}});
    }
    const QByteArray bytes = QJsonDocument(QJsonObject{{"schemaVersion", SchemaVersion}, {"theme", QLatin1String(theme)},
        {"projectRoot", settings.projectRoot}, {"recentProjects", recents}}).toJson(QJsonDocument::Compact);
    if (bytes.size() > MaxBytes) Fail(QStringLiteral("GUI settings exceed the 512 KiB limit. Shorten cached labels or remove recent entries and retry."));
    QSaveFile output(file);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit())
        Fail(QStringLiteral("Cannot atomically save GUI settings: %1. Repair local settings folder access and retry.").arg(output.errorString()));
}
} // namespace

LocalSettings::LocalSettings()
    : LocalSettings(QDir(KnownFolder(FOLDERID_LocalAppData)).filePath(QStringLiteral("MonsterEngine/MonsterStudio/gui-settings-v1.json"))) {}

LocalSettings::LocalSettings(const QString& settingsFile) : file_(AbsolutePath(settingsFile)) {}

QString LocalSettings::DefaultProjectRoot() {
    return QDir(KnownFolder(FOLDERID_Documents)).filePath(QStringLiteral("MonsterStudio/Projects"));
}

LocalSettingsSnapshot LocalSettings::Load() {
    std::lock_guard lock(mutex_);
    try { return Read(file_); }
    catch (const std::exception& error) {
        LocalSettingsSnapshot result;
        result.warning = QString::fromUtf8(error.what()) + QStringLiteral(" Settings file: ") + file_;
        try { result.projectRoot = DefaultProjectRoot(); }
        catch (const std::exception& folderError) { result.warning += QStringLiteral(" ") + QString::fromUtf8(folderError.what()); }
        return result;
    }
}

void LocalSettings::Update(Change change, ThemePreference theme, const RecentProject& recent,
                           const QString& first, const QString& second) {
    std::lock_guard lock(mutex_);
    const QString parent = QFileInfo(file_).absolutePath();
    if (!QDir().mkpath(parent)) Fail(QStringLiteral("Cannot create the local GUI settings folder. Repair its permissions and retry."));
    QLockFile writer(file_ + QStringLiteral(".lock"));
    if (!writer.tryLock(0)) Fail(QStringLiteral("GUI settings are being updated by another process or its lock is unavailable. Retry after that writer finishes."));
    auto settings = Read(file_); // Re-read under the writer lock; never overwrite damaged/unknown bytes.
    if (change == Change::Theme) {
        if (theme != ThemePreference::System && theme != ThemePreference::Light && theme != ThemePreference::Dark)
            Fail(QStringLiteral("Choose System, Light or Dark appearance."));
        settings.theme = theme;
    } else if (change == Change::Root) settings.projectRoot = AbsolutePath(first);
    else if (change == Change::Open) {
        RecentProject row = recent;
        row.directory = AbsolutePath(row.directory);
        CheckString(row.name); CheckString(row.projectId, true); CheckString(row.lastObservedState);
        row.lastOpenedUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        const auto key = PathKey(row.directory);
        auto& rows = settings.recentProjects;
        rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const auto& old) {
            return old.projectId == row.projectId || PathKey(old.directory) == key;
        }), rows.end());
        rows.prepend(row);
        while (rows.size() > MaxRecents) rows.removeLast();
    } else {
        const auto oldKey = PathKey(first);
        auto& rows = settings.recentProjects;
        if (change == Change::Remove) {
            rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const auto& row) { return PathKey(row.directory) == oldKey; }), rows.end());
        } else {
            const QString destination = AbsolutePath(second);
            const auto newKey = PathKey(destination);
            const auto found = std::find_if(rows.begin(), rows.end(), [&](const auto& row) { return PathKey(row.directory) == oldKey; });
            if (found == rows.end()) Fail(QStringLiteral("The recent entry no longer exists. Refresh navigation and retry relocation."));
            const QString projectId = found->projectId;
            // Never discard another project's navigation identity on a path collision.
            for (const auto& row : rows)
                if (row.projectId != projectId && PathKey(row.directory) == newKey)
                    Fail(QStringLiteral("That location already belongs to another recent project. Explicitly open and verify the intended project first."));
            for (auto& row : rows) if (row.projectId == projectId) row.directory = destination;
        }
    }
    Write(file_, settings);
}

void LocalSettings::SaveTheme(ThemePreference theme) { Update(Change::Theme, theme, {}, {}, {}); }
void LocalSettings::SaveProjectRoot(const QString& root) { Update(Change::Root, ThemePreference::System, {}, root, {}); }
void LocalSettings::RecordOpened(const RecentProject& recent) { Update(Change::Open, ThemePreference::System, recent, {}, {}); }
void LocalSettings::RemoveRecent(const QString& directory) { Update(Change::Remove, ThemePreference::System, {}, directory, {}); }
void LocalSettings::RelocateRecent(const QString& oldDirectory, const QString& newDirectory) {
    Update(Change::Relocate, ThemePreference::System, {}, oldDirectory, newDirectory);
}

} // namespace MonsterStudioQt
