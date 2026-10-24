#pragma once

#include <QList>
#include <QString>
#include <mutex>

namespace MonsterStudioQt {

/** Persisted appearance preference; System follows the OS. */
enum class ThemePreference { System, Light, Dark };

/** Navigation cache from a successful explicit create/open, never project truth. */
struct RecentProject {
    QString directory;          ///< Absolute project location, not a lease.
    QString name;               ///< Last observed display name.
    QString projectId;          ///< Last verified project identity.
    QString lastObservedState;  ///< Cached state at the last explicit open.
    QString lastOpenedUtc;      ///< Explicit open time, not completion time.
};

/** Small settings copy; warning describes an unreadable/preserved store. */
struct LocalSettingsSnapshot {
    ThemePreference theme{ThemePreference::System}; ///< Appearance preference.
    QString projectRoot;                          ///< Suggested creation parent.
    QList<RecentProject> recentProjects;           ///< Newest explicit opens first.
    QString warning;                              ///< Actionable load failure.
};

/** Bounded synchronous local IO. Call IO methods on the service worker.
 * No method opens projects, takes project leases, enumerates roots or moves data.
 * Writes throw std::runtime_error on failure and preserve invalid/unknown stores.
 */
class LocalSettings final {
public:
    LocalSettings();
    /** Explicit file injection for isolated consumers; no CLI/environment override. */
    explicit LocalSettings(const QString& settingsFile);
    LocalSettingsSnapshot Load();
    void SaveTheme(ThemePreference);
    void SaveProjectRoot(const QString&);
    void RecordOpened(const RecentProject&);
    void RemoveRecent(const QString& directory);
    void RelocateRecent(const QString& oldDirectory, const QString& newDirectory);
    /** Resolve redirected Windows Documents without creating the project root. */
    static QString DefaultProjectRoot();

private:
    enum class Change { Theme, Root, Open, Remove, Relocate };
    void Update(Change, ThemePreference, const RecentProject&, const QString&, const QString&);
    QString file_;
    std::mutex mutex_;
};

} // namespace MonsterStudioQt
