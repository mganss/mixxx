#include "library/trackset/baseplaylistfeature.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>

#include "controllers/keyboard/keyboardeventfilter.h"
#include "library/export/trackexportwizard.h"
#include "library/library.h"
#include "library/parser.h"
#include "library/parsercsv.h"
#include "library/parserm3u.h"
#include "library/parserpls.h"
#include "library/playlisttablemodel.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "library/treeitem.h"
#include "library/treeitemmodel.h"
#include "moc_baseplaylistfeature.cpp"
#include "track/track.h"
#include "util/assert.h"
#include "util/file.h"
#include "widget/wlibrary.h"
#include "widget/wlibrarysidebar.h"
#include "widget/wlibrarytextbrowser.h"

namespace {
constexpr QChar kUnsafeFilenameReplacement = '-';
const ConfigKey kConfigKeyLastImportExportPlaylistDirectory(
        "[Library]", "LastImportExportPlaylistDirectory");

} // anonymous namespace

BasePlaylistFeature::BasePlaylistFeature(
        Library* pLibrary,
        UserSettingsPointer pConfig,
        PlaylistTableModel* pModel,
        const QString& rootViewName,
        const QString& iconName)
        : BaseTrackSetFeature(pLibrary, pConfig, rootViewName, iconName),
          m_playlistDao(pLibrary->trackCollectionManager()
                                ->internalCollection()
                                ->getPlaylistDAO()),
          m_pPlaylistTableModel(pModel) {
    pModel->setParent(this);

    initActions();
}

void BasePlaylistFeature::initActions() {
    m_pCreatePlaylistAction = new QAction(tr("Create New Playlist"), this);
    connect(m_pCreatePlaylistAction,
            &QAction::triggered,
            this,
            &BasePlaylistFeature::slotCreatePlaylist);

    m_pRenamePlaylistAction = new QAction(tr("Rename"), this);
    connect(m_pRenamePlaylistAction,
            &QAction::triggered,
            this,
            &BasePlaylistFeature::slotRenamePlaylist);
    m_pDuplicatePlaylistAction = new QAction(tr("Duplicate"), this);
    connect(m_pDuplicatePlaylistAction,
            &QAction::triggered,
            this,
            &BasePlaylistFeature::slotDuplicatePlaylist);
    m_pDeletePlaylistAction = new QAction(tr("Remove"), this);
    connect(m_pDeletePlaylistAction,
            &QAction::triggered,
            this,
            &BasePlaylistFeature::slotDeletePlaylist);
    m_pLockPlaylistAction = new QAction(tr("Lock"), this);
    connect(m_pLockPlaylistAction,
            &QAction::triggered,
            this,
            &BasePlaylistFeature::slotTogglePlaylistLock);

    m_pAddToAutoDJAction = new QAction(tr("Add to Auto DJ Queue (bottom)"), this);
    connect(m_pAddToAutoDJAction,
            &QAction::triggered,
            this,
            &BasePlaylistFeature::slotAddToAutoDJ);
    m_pAddToAutoDJTopAction = new QAction(tr("Add to Auto DJ Queue (top)"), this);
    connect(m_pAddToAutoDJTopAction,
            &QAction::triggered,
            this,
            &BasePlaylistFeature::slotAddToAutoDJTop);
    m_pAddToAutoDJReplaceAction = new QAction(tr("Add to Auto DJ Queue (replace)"), this);
    connect(m_pAddToAutoDJReplaceAction,
            &QAction::triggered,
            this,
            &BasePlaylistFeature::slotAddToAutoDJReplace);

    m_pAnalyzePlaylistAction = new QAction(tr("Analyze entire Playlist"), this);
    connect(m_pAnalyzePlaylistAction,
            &QAction::triggered,
            this,
            &BasePlaylistFeature::slotAnalyzePlaylist);

    m_pImportPlaylistAction = new QAction(tr("Import Playlist"), this);
    connect(m_pImportPlaylistAction,
            &QAction::triggered,
            this,
            &BasePlaylistFeature::slotImportPlaylist);
    m_pCreateImportPlaylistAction = new QAction(tr("Import Playlist"), this);
    connect(m_pCreateImportPlaylistAction,
            &QAction::triggered,
            this,
            &BasePlaylistFeature::slotCreateImportPlaylist);
    m_pExportPlaylistAction = new QAction(tr("Export Playlist"), this);
    connect(m_pExportPlaylistAction,
            &QAction::triggered,
            this,
            &BasePlaylistFeature::slotExportPlaylist);
    m_pExportTrackFilesAction = new QAction(tr("Export Track Files"), this);
    connect(m_pExportTrackFilesAction,
            &QAction::triggered,
            this,
            &BasePlaylistFeature::slotExportTrackFiles);

    connect(&m_playlistDao,
            &PlaylistDAO::added,
            this,
            &BasePlaylistFeature::slotPlaylistTableChangedAndSelect);
    connect(&m_playlistDao,
            &PlaylistDAO::lockChanged,
            this,
            &BasePlaylistFeature::slotPlaylistTableChangedAndScrollTo);
    connect(&m_playlistDao,
            &PlaylistDAO::deleted,
            this,
            &BasePlaylistFeature::slotPlaylistTableChanged);
    connect(&m_playlistDao,
            &PlaylistDAO::tracksChanged,
            this,
            &BasePlaylistFeature::slotPlaylistContentChanged);
    connect(&m_playlistDao,
            &PlaylistDAO::renamed,
            this,
            &BasePlaylistFeature::slotPlaylistTableRenamed);

    connect(m_pLibrary,
            &Library::trackSelected,
            this,
            [this](const TrackPointer& pTrack) {
                const auto trackId = pTrack ? pTrack->getId() : TrackId{};
                slotTrackSelected(trackId);
            });
    connect(m_pLibrary,
            &Library::switchToView,
            this,
            &BasePlaylistFeature::slotResetSelectedTrack);
}

int BasePlaylistFeature::playlistIdFromIndex(const QModelIndex& index) {
    TreeItem* item = static_cast<TreeItem*>(index.internalPointer());
    if (item == nullptr) {
        return kInvalidPlaylistId;
    }

    bool ok = false;
    int playlistId = item->getData().toInt(&ok);
    if (ok) {
        return playlistId;
    } else {
        return kInvalidPlaylistId;
    }
}

void BasePlaylistFeature::selectPlaylistInSidebar(int playlistId, bool select) {
    if (!m_pSidebarWidget) {
        return;
    }
    if (playlistId == kInvalidPlaylistId) {
        return;
    }
    QModelIndex index = indexFromPlaylistId(playlistId);
    if (index.isValid() && m_pSidebarWidget) {
        m_pSidebarWidget->selectChildIndex(index, select);
    }
}

void BasePlaylistFeature::activateChild(const QModelIndex& index) {
    //qDebug() << "BasePlaylistFeature::activateChild()" << index;
    int playlistId = playlistIdFromIndex(index);
    if (playlistId == kInvalidPlaylistId) {
        // This happens if user clicks on group nodes
        // like the year folder in the history feature
        return;
    }
    emit saveModelState();
    m_pPlaylistTableModel->setTableModel(playlistId);
    emit showTrackModel(m_pPlaylistTableModel);
    emit enableCoverArtDisplay(true);
}

void BasePlaylistFeature::activatePlaylist(int playlistId) {
    VERIFY_OR_DEBUG_ASSERT(playlistId != kInvalidPlaylistId) {
        return;
    }
    QModelIndex index = indexFromPlaylistId(playlistId);
    //qDebug() << "BasePlaylistFeature::activatePlaylist()" << playlistId << index;
    VERIFY_OR_DEBUG_ASSERT(index.isValid()) {
        return;
    }
    emit saveModelState();
    m_lastRightClickedIndex = index;
    m_pPlaylistTableModel->setTableModel(playlistId);
    emit showTrackModel(m_pPlaylistTableModel);
    emit enableCoverArtDisplay(true);
    // Update selection
    emit featureSelect(this, m_lastRightClickedIndex);
    if (!m_pSidebarWidget) {
        return;
    }
    m_pSidebarWidget->selectChildIndex(m_lastRightClickedIndex);
}

void BasePlaylistFeature::slotRenamePlaylist() {
    int playlistId = playlistIdFromIndex(m_lastRightClickedIndex);
    if (playlistId == kInvalidPlaylistId) {
        return;
    }
    QString oldName = m_playlistDao.getPlaylistName(playlistId);
    bool locked = m_playlistDao.isPlaylistLocked(playlistId);

    if (locked) {
        qDebug() << "Skipping playlist rename because playlist" << playlistId
                 << "is locked.";
        return;
    }
    QString newName;
    bool validNameGiven = false;

    while (!validNameGiven) {
        bool ok = false;
        newName = QInputDialog::getText(nullptr,
                tr("Rename Playlist"),
                tr("Enter new name for playlist:"),
                QLineEdit::Normal,
                oldName,
                &ok)
                          .trimmed();
        if (!ok || oldName == newName) {
            return;
        }

        int existingId = m_playlistDao.getPlaylistIdFromName(newName);

        if (existingId != kInvalidPlaylistId) {
            QMessageBox::warning(nullptr,
                    tr("Renaming Playlist Failed"),
                    tr("A playlist by that name already exists."));
        } else if (newName.isEmpty()) {
            QMessageBox::warning(nullptr,
                    tr("Renaming Playlist Failed"),
                    tr("A playlist cannot have a blank name."));
        } else {
            validNameGiven = true;
        }
    }

    m_playlistDao.renamePlaylist(playlistId, newName);
}

void BasePlaylistFeature::slotDuplicatePlaylist() {
    int oldPlaylistId = playlistIdFromIndex(m_lastRightClickedIndex);
    if (oldPlaylistId == kInvalidPlaylistId) {
        return;
    }

    QString oldName = m_playlistDao.getPlaylistName(oldPlaylistId);

    QString name;
    bool validNameGiven = false;

    while (!validNameGiven) {
        bool ok = false;
        name = QInputDialog::getText(nullptr,
                tr("Duplicate Playlist"),
                tr("Enter name for new playlist:"),
                QLineEdit::Normal,
                //: Appendix to default name when duplicating a playlist
                oldName + tr("_copy", "//:"),
                &ok)
                       .trimmed();
        if (!ok || oldName == name) {
            return;
        }

        int existingId = m_playlistDao.getPlaylistIdFromName(name);

        if (existingId != kInvalidPlaylistId) {
            QMessageBox::warning(nullptr,
                    tr("Playlist Creation Failed"),
                    tr("A playlist by that name already exists."));
        } else if (name.isEmpty()) {
            QMessageBox::warning(nullptr,
                    tr("Playlist Creation Failed"),
                    tr("A playlist cannot have a blank name."));
        } else {
            validNameGiven = true;
        }
    }

    int newPlaylistId = m_playlistDao.createPlaylist(name);

    if (newPlaylistId != kInvalidPlaylistId &&
            m_playlistDao.copyPlaylistTracks(oldPlaylistId, newPlaylistId)) {
        activatePlaylist(newPlaylistId);
    }
}

void BasePlaylistFeature::slotTogglePlaylistLock() {
    int playlistId = playlistIdFromIndex(m_lastRightClickedIndex);
    if (playlistId == kInvalidPlaylistId) {
        return;
    }
    bool locked = !m_playlistDao.isPlaylistLocked(playlistId);

    if (!m_playlistDao.setPlaylistLocked(playlistId, locked)) {
        qDebug() << "Failed to toggle lock of playlistId " << playlistId;
    }
}

void BasePlaylistFeature::slotCreatePlaylist() {
    QString name;
    bool validNameGiven = false;

    while (!validNameGiven) {
        bool ok = false;
        name = QInputDialog::getText(nullptr,
                tr("Create New Playlist"),
                tr("Enter name for new playlist:"),
                QLineEdit::Normal,
                tr("New Playlist"),
                &ok)
                       .trimmed();
        if (!ok) {
            return;
        }

        int existingId = m_playlistDao.getPlaylistIdFromName(name);

        if (existingId != kInvalidPlaylistId) {
            QMessageBox::warning(nullptr,
                    tr("Playlist Creation Failed"),
                    tr("A playlist by that name already exists."));
        } else if (name.isEmpty()) {
            QMessageBox::warning(nullptr,
                    tr("Playlist Creation Failed"),
                    tr("A playlist cannot have a blank name."));
        } else {
            validNameGiven = true;
        }
    }

    int playlistId = m_playlistDao.createPlaylist(name);

    if (playlistId != kInvalidPlaylistId) {
        activatePlaylist(playlistId);
    } else {
        QMessageBox::warning(nullptr,
                tr("Playlist Creation Failed"),
                tr("An unknown error occurred while creating playlist: ") + name);
    }
}

/// Returns a playlist that is a sibling inside the same parent
/// as the start index
int BasePlaylistFeature::getSiblingPlaylistIdOf(QModelIndex& start) {
    QModelIndex nextIndex = start.sibling(start.row() + 1, start.column());
    if (!nextIndex.isValid() && start.row() > 0) {
        // No playlist below, looking above.
        nextIndex = start.sibling(start.row() - 1, start.column());
    }
    if (nextIndex.isValid()) {
        TreeItem* pTreeItem = m_pSidebarModel->getItem(nextIndex);
        DEBUG_ASSERT(pTreeItem != nullptr);
        if (!pTreeItem->hasChildren()) {
            return playlistIdFromIndex(nextIndex);
        }
    }
    return kInvalidPlaylistId;
}

void BasePlaylistFeature::slotDeletePlaylist() {
    //qDebug() << "slotDeletePlaylist() row:" << m_lastRightClickedIndex.data();
    if (!m_lastRightClickedIndex.isValid()) {
        return;
    }

    int playlistId = playlistIdFromIndex(m_lastRightClickedIndex);
    if (playlistId == kInvalidPlaylistId) {
        return;
    }

    // we will switch to the sibling if the deleted playlist is currently active
    bool wasActive = m_pPlaylistTableModel->getPlaylist() == playlistId;

    VERIFY_OR_DEBUG_ASSERT(playlistId >= 0) {
        return;
    }

    bool locked = m_playlistDao.isPlaylistLocked(playlistId);
    if (locked) {
        qDebug() << "Skipping playlist deletion because playlist" << playlistId << "is locked.";
        return;
    }

    int siblingId = getSiblingPlaylistIdOf(m_lastRightClickedIndex);

    m_playlistDao.deletePlaylist(playlistId);

    if (siblingId == kInvalidPlaylistId) {
        return;
    }
    if (wasActive) {
        activatePlaylist(siblingId);
    } else if (m_pSidebarWidget) {
        m_pSidebarWidget->selectChildIndex(indexFromPlaylistId(siblingId), false);
    }
}

void BasePlaylistFeature::slotImportPlaylist() {
    //qDebug() << "slotImportPlaylist() row:" << m_lastRightClickedIndex.data();
    const QString playlistFile = getPlaylistFile();
    if (playlistFile.isEmpty()) {
        return;
    }

    // Update the import/export playlist directory
    QString fileDirectory(playlistFile);
    fileDirectory.truncate(playlistFile.lastIndexOf(QDir::separator()));
    m_pConfig->set(kConfigKeyLastImportExportPlaylistDirectory,
            ConfigValue(fileDirectory));

    slotImportPlaylistFile(playlistFile);
    activateChild(m_lastRightClickedIndex);
}

void BasePlaylistFeature::slotImportPlaylistFile(const QString& playlist_file) {
    // The user has picked a new directory via a file dialog. This means the
    // system sandboxer (if we are sandboxed) has granted us permission to this
    // folder. We don't need access to this file on a regular basis so we do not
    // register a security bookmark.

    QList<QString> locations = Parser::parse(playlist_file);
    // Iterate over the List that holds locations of playlist entries
    m_pPlaylistTableModel->addTracks(QModelIndex(), locations);
}

void BasePlaylistFeature::slotCreateImportPlaylist() {
    // Get file to read
    const QStringList playlistFiles = LibraryFeature::getPlaylistFiles();
    if (playlistFiles.isEmpty()) {
        return;
    }

    // Set last import directory
    QString fileDirectory(playlistFiles.first());
    fileDirectory.truncate(playlistFiles.first().lastIndexOf(QDir::separator()));
    m_pConfig->set(kConfigKeyLastImportExportPlaylistDirectory,
            ConfigValue(fileDirectory));

    int lastPlaylistId = kInvalidPlaylistId;

    // For each selected element create a different playlist.
    for (const QString& playlistFile : playlistFiles) {
        const QFileInfo fileInfo(playlistFile);
        // Get a valid name
        const QString baseName = fileInfo.baseName();
        QString name;

        bool validNameGiven = false;
        int i = 0;
        while (!validNameGiven) {
            name = baseName;
            if (i != 0) {
                name += QString::number(i);
            }

            // Check name
            int existingId = m_playlistDao.getPlaylistIdFromName(name);

            validNameGiven = (existingId == kInvalidPlaylistId);
            ++i;
        }

        lastPlaylistId = m_playlistDao.createPlaylist(name);
        if (lastPlaylistId != kInvalidPlaylistId) {
            emit saveModelState();
            m_pPlaylistTableModel->setTableModel(lastPlaylistId);
        } else {
            QMessageBox::warning(nullptr,
                    tr("Playlist Creation Failed"),
                    tr("An unknown error occurred while creating playlist: ") + name);
            return;
        }

        slotImportPlaylistFile(playlistFile);
    }
    activatePlaylist(lastPlaylistId);
}

void BasePlaylistFeature::slotExportPlaylist() {
    int playlistId = playlistIdFromIndex(m_lastRightClickedIndex);
    if (playlistId == kInvalidPlaylistId) {
        return;
    }
    QString playlistName = m_playlistDao.getPlaylistName(playlistId);
    // replace separator character with something generic
    playlistName = playlistName.replace(QDir::separator(), kUnsafeFilenameReplacement);
    qDebug() << "Export playlist" << playlistName;

    QString lastPlaylistDirectory = m_pConfig->getValue(
            kConfigKeyLastImportExportPlaylistDirectory,
            QStandardPaths::writableLocation(QStandardPaths::MusicLocation));

    // Open a dialog to let the user choose the file location for playlist export.
    // The location is set to the last used directory for import/export and the file
    // name to the playlist name.
    const QString fileLocation = getFilePathWithVerifiedExtensionFromFileDialog(
            tr("Export Playlist"),
            lastPlaylistDirectory.append("/").append(playlistName).append(".m3u"),
            tr("M3U Playlist (*.m3u);;M3U8 Playlist (*.m3u8);;"
               "PLS Playlist (*.pls);;Text CSV (*.csv);;Readable Text (*.txt)"),
            tr("M3U Playlist (*.m3u)"));
    // Exit method if the file name is empty because the user cancelled the save dialog.
    if (fileLocation.isEmpty()) {
        return;
    }

    // Update the import/export playlist directory
    QString fileDirectory(fileLocation);
    fileDirectory.truncate(fileLocation.lastIndexOf(QDir::separator()));
    m_pConfig->set(kConfigKeyLastImportExportPlaylistDirectory,
            ConfigValue(fileDirectory));

    // The user has picked a new directory via a file dialog. This means the
    // system sandboxer (if we are sandboxed) has granted us permission to this
    // folder. We don't need access to this file on a regular basis so we do not
    // register a security bookmark.

    // Create a new table model since the main one might have an active search.
    // This will only export songs that we think exist on default
    QScopedPointer<PlaylistTableModel> pPlaylistTableModel(
            new PlaylistTableModel(this,
                    m_pLibrary->trackCollectionManager(),
                    "mixxx.db.model.playlist_export"));

    emit saveModelState();
    pPlaylistTableModel->setTableModel(m_pPlaylistTableModel->getPlaylist());
    pPlaylistTableModel->setSort(
            pPlaylistTableModel->fieldIndex(
                    ColumnCache::COLUMN_PLAYLISTTRACKSTABLE_POSITION),
            Qt::AscendingOrder);
    pPlaylistTableModel->select();

    // check config if relative paths are desired
    bool useRelativePath = m_pConfig->getValue<bool>(
            ConfigKey("[Library]", "UseRelativePathOnExport"));

    if (fileLocation.endsWith(".csv", Qt::CaseInsensitive)) {
        ParserCsv::writeCSVFile(fileLocation, pPlaylistTableModel.data(), useRelativePath);
    } else if (fileLocation.endsWith(".txt", Qt::CaseInsensitive)) {
        if (m_playlistDao.getHiddenType(pPlaylistTableModel->getPlaylist()) ==
                PlaylistDAO::PLHT_SET_LOG) {
            ParserCsv::writeReadableTextFile(fileLocation, pPlaylistTableModel.data(), true);
        } else {
            ParserCsv::writeReadableTextFile(fileLocation, pPlaylistTableModel.data(), false);
        }
    } else {
        // Create and populate a list of files of the playlist
        QList<QString> playlistItems;
        int rows = pPlaylistTableModel->rowCount();
        for (int i = 0; i < rows; ++i) {
            QModelIndex index = pPlaylistTableModel->index(i, 0);
            playlistItems << pPlaylistTableModel->getTrackLocation(index);
        }
        exportPlaylistItemsIntoFile(
                fileLocation,
                playlistItems,
                useRelativePath);
    }
}

void BasePlaylistFeature::slotExportTrackFiles() {
    QScopedPointer<PlaylistTableModel> pPlaylistTableModel(
            new PlaylistTableModel(this,
                    m_pLibrary->trackCollectionManager(),
                    "mixxx.db.model.playlist_export"));

    emit saveModelState();
    pPlaylistTableModel->setTableModel(m_pPlaylistTableModel->getPlaylist());
    pPlaylistTableModel->setSort(pPlaylistTableModel->fieldIndex(
                                         ColumnCache::COLUMN_PLAYLISTTRACKSTABLE_POSITION),
            Qt::AscendingOrder);
    pPlaylistTableModel->select();

    int rows = pPlaylistTableModel->rowCount();
    TrackPointerList tracks;
    for (int i = 0; i < rows; ++i) {
        QModelIndex index = pPlaylistTableModel->index(i, 0);
        tracks.push_back(pPlaylistTableModel->getTrack(index));
    }

    TrackExportWizard track_export(nullptr, m_pConfig, tracks);
    track_export.exportTracks();
}

void BasePlaylistFeature::slotAddToAutoDJ() {
    //qDebug() << "slotAddToAutoDJ() row:" << m_lastRightClickedIndex.data();
    addToAutoDJ(PlaylistDAO::AutoDJSendLoc::BOTTOM);
}

void BasePlaylistFeature::slotAddToAutoDJTop() {
    //qDebug() << "slotAddToAutoDJTop() row:" << m_lastRightClickedIndex.data();
    addToAutoDJ(PlaylistDAO::AutoDJSendLoc::TOP);
}

void BasePlaylistFeature::slotAddToAutoDJReplace() {
    //qDebug() << "slotAddToAutoDJReplace() row:" << m_lastRightClickedIndex.data();
    addToAutoDJ(PlaylistDAO::AutoDJSendLoc::REPLACE);
}

void BasePlaylistFeature::addToAutoDJ(PlaylistDAO::AutoDJSendLoc loc) {
    //qDebug() << "slotAddToAutoDJ() row:" << m_lastRightClickedIndex.data();
    if (m_lastRightClickedIndex.isValid()) {
        int playlistId = playlistIdFromIndex(m_lastRightClickedIndex);
        if (playlistId >= 0) {
            // Insert this playlist
            m_playlistDao.addPlaylistToAutoDJQueue(playlistId, loc);
        }
    }
}

void BasePlaylistFeature::slotAnalyzePlaylist() {
    if (m_lastRightClickedIndex.isValid()) {
        int playlistId = playlistIdFromIndex(m_lastRightClickedIndex);
        if (playlistId >= 0) {
            QList<TrackId> ids = m_playlistDao.getTrackIds(playlistId);
            emit analyzeTracks(ids);
        }
    }
}

TreeItemModel* BasePlaylistFeature::sidebarModel() const {
    return m_pSidebarModel;
}

void BasePlaylistFeature::bindLibraryWidget(WLibrary* libraryWidget,
        KeyboardEventFilter* keyboard) {
    Q_UNUSED(keyboard);
    WLibraryTextBrowser* edit = new WLibraryTextBrowser(libraryWidget);
    edit->setHtml(getRootViewHtml());
    edit->setOpenLinks(false);
    connect(edit,
            &WLibraryTextBrowser::anchorClicked,
            this,
            &BasePlaylistFeature::htmlLinkClicked);
    libraryWidget->registerView(m_rootViewName, edit);
    m_pLibrary->bindFeatureRootView(edit);
}

void BasePlaylistFeature::bindSidebarWidget(WLibrarySidebar* pSidebarWidget) {
    // store the sidebar widget pointer for later use in onRightClickChild
    DEBUG_ASSERT(!m_pSidebarWidget);
    m_pSidebarWidget = pSidebarWidget;
}

void BasePlaylistFeature::htmlLinkClicked(const QUrl& link) {
    if (QString(link.path()) == "create") {
        slotCreatePlaylist();
    } else {
        qDebug() << "Unknown playlist link clicked" << link.path();
    }
}

void BasePlaylistFeature::updateChildModel(int playlistId) {
    QString playlistLabel = fetchPlaylistLabel(playlistId);

    QVariant variantId = QVariant(playlistId);

    for (int row = 0; row < m_pSidebarModel->rowCount(); ++row) {
        QModelIndex index = m_pSidebarModel->index(row, 0);
        TreeItem* pTreeItem = m_pSidebarModel->getItem(index);
        DEBUG_ASSERT(pTreeItem != nullptr);
        if (!pTreeItem->hasChildren() && // leaf node
                pTreeItem->getData() == variantId) {
            pTreeItem->setLabel(playlistLabel);
            decorateChild(pTreeItem, playlistId);
        }
    }
}

/**
  * Clears the child model dynamically, but the invisible root item remains
  */
void BasePlaylistFeature::clearChildModel() {
    m_pSidebarModel->removeRows(0, m_pSidebarModel->rowCount());
}

QModelIndex BasePlaylistFeature::indexFromPlaylistId(int playlistId) {
    QVariant variantId = QVariant(playlistId);
    QModelIndexList results = m_pSidebarModel->match(
            m_pSidebarModel->getRootIndex(),
            TreeItemModel::kDataRole,
            variantId,
            1,
            Qt::MatchWrap | Qt::MatchExactly | Qt::MatchRecursive);
    if (!results.isEmpty()) {
        return results.front();
    }
    return QModelIndex();
}

void BasePlaylistFeature::slotTrackSelected(TrackId trackId) {
    m_selectedTrackId = trackId;
    m_playlistDao.getPlaylistsTrackIsIn(m_selectedTrackId, &m_playlistIdsOfSelectedTrack);

    for (int row = 0; row < m_pSidebarModel->rowCount(); ++row) {
        QModelIndex index = m_pSidebarModel->index(row, 0);
        TreeItem* pTreeItem = m_pSidebarModel->getItem(index);
        DEBUG_ASSERT(pTreeItem != nullptr);
        markTreeItem(pTreeItem);
    }

    m_pSidebarModel->triggerRepaint();
}

void BasePlaylistFeature::markTreeItem(TreeItem* pTreeItem) {
    bool ok;
    int playlistId = pTreeItem->getData().toInt(&ok);
    if (ok) {
        bool shouldBold = m_playlistIdsOfSelectedTrack.contains(playlistId);
        pTreeItem->setBold(shouldBold);
        if (shouldBold && pTreeItem->hasParent()) {
            TreeItem* item = pTreeItem;
            // extra parents, because -Werror=parentheses
            while ((item = item->parent())) {
                item->setBold(true);
            }
        }
    }
    if (pTreeItem->hasChildren()) {
        QList<TreeItem*> children = pTreeItem->children();

        for (int i = 0; i < children.size(); i++) {
            markTreeItem(children.at(i));
        }
    }
}

void BasePlaylistFeature::slotResetSelectedTrack() {
    slotTrackSelected(TrackId{});
}
