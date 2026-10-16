// SPDX-License-Identifier: BSD-2-Clause
#include "core/Derivatives.h"
#include "core/Exporter.h"
#include "core/FileTime.h"
#include "core/Project.h"
#include "core/RecentProjects.h"
#include "imaging/Exif.h"
#include "imaging/ImageIO.h"
#include "imaging/Look.h"
#include "imaging/Mask.h"

#include <QJsonDocument>
#include <QSignalSpy>
#include <QSqlError>
#include <QTemporaryDir>
#include <QtTest>

using namespace pe;

namespace {

const QString kSample = QStringLiteral(PHOTOEDITOR_TEST_DATA "/sample.jpg");
const QString kLegacy = QStringLiteral(PHOTOEDITOR_TEST_DATA "/core/legacy.sqlite3");

/// Projeto descartável com ``n`` cópias da foto de amostra em raw/.
QString makeProject(QTemporaryDir &dir, int n)
{
    const QString root = dir.filePath(QStringLiteral("2026-10-15 Evento"));
    QDir().mkpath(root + QStringLiteral("/raw"));
    for (int i = 0; i < n; ++i)
        QFile::copy(kSample, root + QStringLiteral("/raw/IMG_%1.jpg").arg(i + 1, 4, 10, QLatin1Char('0')));
    return root;
}

} // namespace

class CoreTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qputenv("PHOTOEDITOR_DATA_DIR", m_data.path().toUtf8());
    }

    void scanIsIdempotent()
    {
        QTemporaryDir dir;
        Project project(makeProject(dir, 3));
        ScanSummary s = project.scan();
        QCOMPARE(s.added, 3);
        QCOMPARE(s.total, 3);
        s = project.scan();
        QCOMPARE(s.added, 0);
        QCOMPARE(s.updated, 0);
        const auto photos = project.visiblePhotos();
        QCOMPARE(photos.size(), size_t(3));
        QCOMPARE(photos.front().mtimeNs, fileMtimeNs(project.paths().rawPath(photos.front().fileName)));
        QVERIFY(photos.front().width > 0);
    }

    void lockedProjectRefusesSecondOpen()
    {
        QTemporaryDir dir;
        const QString root = makeProject(dir, 1);
        Project first(root);
        QVERIFY_THROWS_EXCEPTION(ActionError, Project second(root));
    }

    void adjustAndUndo()
    {
        QTemporaryDir dir;
        Project project(makeProject(dir, 1));
        project.scan();
        const Photo p = project.visiblePhotos().front();
        QVERIFY(project.state(p).pristine);
        imaging::Values v = imaging::kNeutral;
        v[imaging::Exposure] = 0.5;
        project.setAdjustments(p, v, QStringLiteral("warm"));
        QCOMPARE(project.state(p).values[imaging::Exposure], 0.5);
        QCOMPARE(project.state(p).preset, QStringLiteral("warm"));
        QCOMPARE(project.history(p).front().kind, HistoryKind::Adjust);
        // Só o preset mudou: a ação é "preset".
        project.setAdjustments(p, v, QStringLiteral("cool"));
        QCOMPARE(project.history(p).front().kind, HistoryKind::Preset);
        QVERIFY(project.undoLast());
        QCOMPARE(project.state(p).preset, QStringLiteral("warm"));
        QVERIFY(project.undoLast());
        QCOMPARE(project.state(p).values[imaging::Exposure], 0.0);
        QVERIFY(!project.undoLast());
    }

    void redoReappliesUntilANewAction()
    {
        QTemporaryDir dir;
        Project project(makeProject(dir, 2));
        project.scan();
        const Photo p = project.visiblePhotos().front();
        imaging::Values v = imaging::kNeutral;
        v[imaging::Contrast] = 25;
        project.setAdjustments(p, v, {});
        project.deletePhoto(project.visiblePhotos().back());
        QVERIFY(!project.nextRedo());
        project.undoLast();  // volta a exclusão
        project.undoLast();  // volta o ajuste
        QCOMPARE(project.state(p).values[imaging::Contrast], 0.0);
        QCOMPARE(project.nextRedo()->kind, HistoryKind::Adjust);
        project.redoLast();
        QCOMPARE(project.state(p).values[imaging::Contrast], 25.0);
        QCOMPARE(project.nextRedo()->kind, HistoryKind::Delete);
        project.redoLast();
        QCOMPARE(project.visiblePhotos().size(), size_t(1));
        QVERIFY(!project.nextRedo());
        // Desfeito de novo e depois uma ação nova: o refazer é descartado.
        project.undoLast();
        QVERIFY(project.nextRedo());
        project.setCover(project.requirePhoto(p.id), true);
        QVERIFY(!project.nextRedo());
    }

    void archiveRestoreAndUndo()
    {
        QTemporaryDir dir;
        Project project(makeProject(dir, 3));
        project.scan();
        const Photo p = project.visiblePhotos().front();
        project.archivePhoto(p);
        QVERIFY(!QFileInfo::exists(project.paths().rawPath(p.fileName)));
        QVERIFY(QFileInfo::exists(project.paths().archivedPath(p.fileName)));
        QCOMPARE(project.visiblePhotos().size(), size_t(2));
        QCOMPARE(project.archivedPhotos().size(), size_t(1));
        // O caminho atual aponta para archived/ (preview e exportação leem dali).
        QCOMPARE(project.requirePhoto(p.id).sourcePath, project.paths().archivedPath(p.fileName));
        // Rescan não a dá por sumida.
        QCOMPARE(project.scan().missing, 0);
        QCOMPARE(project.requirePhoto(p.id).status, PhotoStatus::Archived);
        // Desfazer volta para raw/; refazer arquiva de novo.
        project.undoLast();
        QVERIFY(QFileInfo::exists(project.paths().rawPath(p.fileName)));
        project.redoLast();
        QVERIFY(QFileInfo::exists(project.paths().archivedPath(p.fileName)));
        // Excluir do arquivo: vai para deleted/; desfazer volta ao ARQUIVO.
        project.deletePhoto(project.requirePhoto(p.id));
        QVERIFY(QFileInfo::exists(project.paths().deletedPath(p.fileName)));
        project.undoLast();
        QCOMPARE(project.requirePhoto(p.id).status, PhotoStatus::Archived);
        QVERIFY(QFileInfo::exists(project.paths().archivedPath(p.fileName)));
        // Restaurar: de volta à filmstrip.
        project.restorePhoto(project.requirePhoto(p.id));
        QVERIFY(QFileInfo::exists(project.paths().rawPath(p.fileName)));
        QCOMPARE(project.visiblePhotos().size(), size_t(3));
        QCOMPARE(project.history(p).front().kind, HistoryKind::Restore);
    }

    void purgeAndEmptyTrash()
    {
        QTemporaryDir dir;
        Project project(makeProject(dir, 3));
        project.scan();
        auto photos = project.visiblePhotos();
        const Photo a = photos[0], b = photos[1];
        const Photo copy = project.duplicate(a);
        project.deletePhoto(a);
        project.deletePhoto(b);
        QCOMPARE(project.trashedPhotos().size(), size_t(2));
        // Excluir definitivamente uma: arquivo, linha e a cópia virtual dela.
        QVERIFY(project.purge(project.requirePhoto(a.id)) >= 2);
        QVERIFY(!QFileInfo::exists(project.paths().deletedPath(a.fileName)));
        QVERIFY(!project.photo(a.id));
        QVERIFY(!project.photo(copy.id));
        QVERIFY(project.history(b).size() > 0);
        // Esvaziar a lixeira leva o resto; o desfazer não acha mais nada delas.
        QCOMPARE(project.emptyTrash(), 1);
        QVERIFY(!QFileInfo::exists(project.paths().deletedPath(b.fileName)));
        QVERIFY(project.trashedPhotos().empty());
        QCOMPARE(project.visiblePhotos().size(), size_t(1));
        QVERIFY(!project.undoLast());
        // Foto ativa nunca é apagada por purge.
        QCOMPARE(project.purge(project.visiblePhotos().front()), 0);
    }

    void sliderWithLayersIsAnAdjustment()
    {
        QTemporaryDir dir;
        Project project(makeProject(dir, 1));
        project.scan();
        const Photo p = project.visiblePhotos().front();
        cv::Mat mask(10, 10, CV_32F, cv::Scalar(1.0f));
        const QString key = imaging::saveMask(project.masksDir(), mask);
        QJsonArray layers{QJsonObject{{QStringLiteral("id"), QStringLiteral("abc123")}, {QStringLiteral("mask"), key},
                                      {QStringLiteral("values"), QJsonObject{}}, {QStringLiteral("preset"), QString()}}};
        project.setAdjustments(p, imaging::kNeutral, {}, std::nullopt, layers);
        QCOMPARE(project.history(p).front().kind, HistoryKind::Layer);
        // Slider do restante com camadas gravadas: é ajuste, não "camadas".
        EditState s = project.state(p);
        s.values[imaging::Contrast] = 15;
        project.setAdjustments(p, s.values, s.preset, std::nullopt, imaging::layersToJson(s.layers));
        QCOMPARE(project.history(p).front().kind, HistoryKind::Adjust);
    }

    void lookPersistsAndUndoes()
    {
        QTemporaryDir dir;
        const QString root = makeProject(dir, 1);
        QString photoId;
        imaging::Look look;
        look.levels[imaging::ChannelRgb] = imaging::normalizeLevelsChannel({12, 1.3, 240, 5, 250});
        look.curves[imaging::ChannelRed] = imaging::normalizeCurve({{0, 0.05}, {0.5, 0.6}, {1, 1}});
        look.effects = imaging::normalizeEffects({{QStringLiteral("grain"), 25}, {QStringLiteral("bw"), 50}});
        {
            Project project(root);
            project.scan();
            const Photo p = project.visiblePhotos().front();
            photoId = p.id;
            const EditState s = project.setAdjustments(p, imaging::kNeutral, {}, std::nullopt, std::nullopt, look);
            QVERIFY(s.look == look);
            // Três partes do look de uma vez: "adjust".
            QCOMPARE(project.history(p).front().kind, HistoryKind::Adjust);
            const QJsonObject after = project.history(p).front().after;
            QVERIFY(after.contains(QStringLiteral("levels")));
            QVERIFY(after.contains(QStringLiteral("curves")));
            QCOMPARE(after.value(QStringLiteral("effects")).toArray().size(), 2);
            // Só a curva: "curves"; só os efeitos: "effects"; só os níveis: "levels".
            imaging::Look next = look;
            next.curves[imaging::ChannelRed].clear();
            project.setAdjustments(p, imaging::kNeutral, {}, std::nullopt, std::nullopt, next);
            QCOMPARE(project.history(p).front().kind, HistoryKind::Curves);
            next.effects.clear();
            project.setAdjustments(p, imaging::kNeutral, {}, std::nullopt, std::nullopt, next);
            QCOMPARE(project.history(p).front().kind, HistoryKind::Effects);
            next.levels[imaging::ChannelRgb].gamma = 0.9;
            project.setAdjustments(p, imaging::kNeutral, {}, std::nullopt, std::nullopt, next);
            QCOMPARE(project.history(p).front().kind, HistoryKind::Levels);
            QVERIFY(project.undoLast());
            QVERIFY(project.undoLast());
            QVERIFY(project.undoLast());
            QVERIFY(project.state(p).look == look);
            // Sem o look explícito, o gravado fica (só o slider muda).
            imaging::Values v = imaging::kNeutral;
            v[imaging::Exposure] = 0.3;
            QVERIFY(project.setAdjustments(p, v, {}).look == look);
            QVERIFY(project.undoLast());
            // A renderização usa o look: o hash muda e o render existe.
            QVERIFY(project.state(p).settingsHash() != EditState{}.settingsHash());
            const QString rendered = render({project.paths(), p, project.state(p), std::nullopt, {}});
            QVERIFY(rendered.contains(QStringLiteral("/cache/render/")));
        }
        {
            // Reaberto do disco: igual.
            Project project(root);
            const Photo p = project.requirePhoto(photoId);
            QVERIFY(project.state(p).look == look);
            QVERIFY(project.undoLast());
            QVERIFY(project.state(p).look.isIdentity());
            QVERIFY(project.redoLast());
            QVERIFY(project.state(p).look == look);
            // A cópia leva o look junto.
            QVERIFY(project.state(project.duplicate(p)).look == look);
        }
    }

    void layerLookPersists()
    {
        QTemporaryDir dir;
        Project project(makeProject(dir, 1));
        project.scan();
        const Photo p = project.visiblePhotos().front();
        cv::Mat mask(10, 10, CV_32F, cv::Scalar(1.0f));
        const QString key = imaging::saveMask(project.masksDir(), mask);
        QJsonArray layers{QJsonObject{{QStringLiteral("id"), QStringLiteral("abc123")}, {QStringLiteral("mask"), key},
                                      {QStringLiteral("values"), QJsonObject{}}, {QStringLiteral("preset"), QString()}}};
        project.setAdjustments(p, imaging::kNeutral, {}, std::nullopt, layers);
        EditState s = project.state(p);
        s.layers[0].look.effects = {{QStringLiteral("vignette"), 70}};
        project.setAdjustments(p, s.values, s.preset, std::nullopt, imaging::layersToJson(s.layers));
        QCOMPARE(project.history(p).front().kind, HistoryKind::Effects);
        QCOMPARE(imaging::effectAmount(project.state(p).layers[0].look.effects, QStringLiteral("vignette")), 70);
        project.undoLast();
        QVERIFY(project.state(p).layers[0].look.isIdentity());
    }

    void presetOnlyMovesSliders()
    {
        // O que o EditorController::applyPreset faz: valores + deslocamentos,
        // preset vazio, histórico "preset".
        QTemporaryDir dir;
        Project project(makeProject(dir, 1));
        project.scan();
        const Photo p = project.visiblePhotos().front();
        EditState s = project.state(p);
        s.values[imaging::Contrast] = 90;
        s.values = imaging::effective(s.values, QStringLiteral("vivid"));
        const EditState saved = project.applyState(p, s, HistoryKind::Preset);
        QCOMPARE(saved.preset, QString());
        QCOMPARE(saved.values[imaging::Contrast], 100.0);  // limitado
        QCOMPARE(saved.values[imaging::Vibrance], 30.0);
        QCOMPARE(project.history(p).front().kind, HistoryKind::Preset);
        // Foto com preset legado continua renderizando com ele.
        project.setAdjustments(p, imaging::kNeutral, QStringLiteral("warm"));
        QCOMPARE(project.state(p).preset, QStringLiteral("warm"));
        QVERIFY(!project.state(p).isNeutral());
    }

    void migratesFromStep13()
    {
        QTemporaryDir dir;
        const QString root = makeProject(dir, 1);
        QString photoId;
        {
            Project project(root);
            project.scan();
            const Photo p = project.visiblePhotos().front();
            photoId = p.id;
            imaging::Values v = imaging::kNeutral;
            v[imaging::Contrast] = 40;
            project.setAdjustments(p, v, {});
        }
        {
            // Volta o banco ao passo 13 (sem as colunas do look).
            QSqlDatabase raw = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("step13"));
            raw.setDatabaseName(root + QStringLiteral("/project_data/db.sqlite3"));
            QVERIFY(raw.open());
            QSqlQuery q(raw);
            for (const char *col : {"levels", "curves", "effects"})
                QVERIFY2(q.exec(QStringLiteral("ALTER TABLE photoeditor_adjustment DROP COLUMN %1").arg(QLatin1String(col))),
                         qPrintable(q.lastError().text()));
            QVERIFY(q.exec(QStringLiteral("PRAGMA user_version = 13")));
            raw.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("step13"));
        Project project(root);
        QCOMPARE(project.db().schemaVersion(), 14);
        const Photo p = project.requirePhoto(photoId);
        QCOMPARE(project.state(p).values[imaging::Contrast], 40.0);
        QVERIFY(project.state(p).look.isIdentity());
        QCOMPARE(project.db().scalar(QStringLiteral("SELECT effects FROM photoeditor_adjustment")).toString(),
                 QStringLiteral("[]"));
        // Entrada de histórico antiga (sem as chaves): desfazer dá a identidade.
        QVERIFY(project.undoLast());
        QVERIFY(project.state(p).look.isIdentity());
    }

    void deleteMovesFileAndUndoRestores()
    {
        QTemporaryDir dir;
        Project project(makeProject(dir, 2));
        project.scan();
        const Photo p = project.visiblePhotos().front();
        project.deletePhoto(p);
        QVERIFY(!QFileInfo::exists(project.paths().rawPath(p.fileName)));
        QVERIFY(QFileInfo::exists(project.paths().deletedPath(p.fileName)));
        QCOMPARE(project.visiblePhotos().size(), size_t(1));
        // Rescan não perde a exclusão (o arquivo está em deleted/).
        QCOMPARE(project.scan().missing, 0);
        project.undoLast();
        QVERIFY(QFileInfo::exists(project.paths().rawPath(p.fileName)));
        QCOMPARE(project.visiblePhotos().size(), size_t(2));
    }

    void duplicateCoverAndInstagram()
    {
        QTemporaryDir dir;
        Project project(makeProject(dir, 1));
        project.scan();
        const Photo p = project.visiblePhotos().front();
        const Photo copy = project.duplicate(p);
        QCOMPARE(copy.fileName, QStringLiteral("IMG_0001_copy.jpg"));
        QCOMPARE(project.duplicate(copy).fileName, QStringLiteral("IMG_0001_copy2.jpg"));
        QCOMPARE(project.visiblePhotos().size(), size_t(3));

        QVERIFY(project.setCover(p, true).isCover);
        QCOMPARE(project.cover()->id, p.id);
        project.setCover(copy, true);
        QCOMPARE(project.cover()->id, copy.id);
        project.undoLast();
        QCOMPARE(project.cover()->id, p.id);

        auto [version, created] = project.createInstagramVersion(p);
        QVERIFY(created);
        QCOMPARE(version.fileName, QStringLiteral("IMG_0001_instagram.jpg"));
        QVERIFY(project.state(version).crop.ratio > 0);
        QVERIFY(!project.createInstagramVersion(p).second);
        QVERIFY_THROWS_EXCEPTION(ActionError, project.duplicate(version));
    }

    void renderAndExportIncremental()
    {
        QTemporaryDir dir;
        Project project(makeProject(dir, 2));
        project.scan();
        const Photo p = project.visiblePhotos().front();
        imaging::Values v = imaging::kNeutral;
        v[imaging::Contrast] = 30;
        const EditState s = project.setAdjustments(p, v, {});
        const QString rendered = render({project.paths(), p, s, std::nullopt, {}});
        QVERIFY(rendered.contains(QStringLiteral("/cache/render/")));
        QVERIFY(QFileInfo(rendered).isFile());
        QVERIFY(QFileInfo(ensureThumbnail(project.paths(), p)).isFile());

        project.setCover(p, true);
        Exporter exporter(&project);
        QSignalSpy done(&exporter, &Exporter::finished);
        exporter.start();
        QVERIFY(done.wait(60000));
        QCOMPARE(exporter.status().written, 2);
        const QString out = QDir(project.paths().publishDir()).filePath(p.fileName);
        QVERIFY(QFileInfo(out).isFile());
        QVERIFY(QFileInfo(QDir(project.paths().publishDir()).filePath(QStringLiteral("capa.jpg"))).isFile());
        const auto exif = imaging::parseExif(imaging::readJpegHeader(out)->exif);
        QVERIFY(exif.software.startsWith(QStringLiteral("PhotoEditor ")));

        exporter.start();
        QVERIFY(done.wait(60000));
        QCOMPARE(exporter.status().written, 0);
        QCOMPARE(exporter.status().skipped, 2);

        project.deletePhoto(project.requirePhoto(p.id));
        exporter.start();
        QVERIFY(done.wait(60000));
        QCOMPARE(exporter.status().removed, 1);
        QVERIFY(!QFileInfo::exists(out));
        QVERIFY(!QFileInfo::exists(QDir(project.paths().publishDir()).filePath(QStringLiteral("capa.jpg"))));
    }

    void legacyCatalogOpensWithoutChanges()
    {
        if (!QFileInfo::exists(kLegacy))
            QSKIP("no legacy fixture");
        QTemporaryDir dir;
        const QString root = makeProject(dir, 0);
        QDir().mkpath(root + QStringLiteral("/project_data"));
        QFile::copy(kLegacy, root + QStringLiteral("/project_data/db.sqlite3"));
        QFile(root + QStringLiteral("/project_data/db.sqlite3")).setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        Project project(root);
        QCOMPARE(project.db().schemaVersion(), Database::latestSchemaVersion());

        // O que o Python exportou continua "em dia": mesmo settings_hash e
        // mesmo exported_hash calculados a partir do banco dele.
        QFile f(QStringLiteral(PHOTOEDITOR_TEST_DATA "/core/legacy-expected.json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonArray expected = QJsonDocument::fromJson(f.readAll()).array();
        QCOMPARE(expected.size(), 5);
        for (const auto &v : expected) {
            const QJsonObject e = v.toObject();
            const Photo p = project.requirePhoto(e.value(QStringLiteral("id")).toString());
            const EditState s = project.state(p);
            QCOMPARE(s.settingsHash(), e.value(QStringLiteral("settings")).toString());
            // Sem níveis/curvas/efeitos o hash é o do Python; com eles, outro.
            QVERIFY(s.look.isIdentity());
            EditState leveled = s;
            leveled.look.levels[imaging::ChannelRgb].inBlack = 8;
            QVERIFY(leveled.settingsHash() != s.settingsHash());
            QCOMPARE(p.exportedHash, e.value(QStringLiteral("exported_hash")).toString());
            QCOMPARE(Exporter::exportHash(p, s, project.mergeVersion(project.mergeForPhoto(p))), p.exportedHash);
        }
        const auto cover = project.cover();
        QVERIFY(cover && cover->fileName == QStringLiteral("IMG_0002.jpg"));
        const auto copy = project.visiblePhotos();
        QCOMPARE(copy.size(), size_t(5));
    }

    void oldDjangoCatalogMigratesIncrementally()
    {
        // Banco parado na migration 0009 (antes de cópia/capa/Instagram).
        QTemporaryDir dir;
        const QString root = makeProject(dir, 0);
        QDir().mkpath(root + QStringLiteral("/project_data"));
        {
            Database db;
            db.open(root + QStringLiteral("/project_data/db.sqlite3"));
            // open() já migra tudo; recria-se um banco "Django 0009" à mão.
        }
        QFile::remove(root + QStringLiteral("/project_data/db.sqlite3"));
        {
            QSqlDatabase raw = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("old"));
            raw.setDatabaseName(root + QStringLiteral("/project_data/db.sqlite3"));
            QVERIFY(raw.open());
            QSqlQuery q(raw);
            q.exec(QStringLiteral("CREATE TABLE django_migrations (id integer primary key, app varchar, name varchar, "
                                  "applied datetime)"));
            const QStringList names{QStringLiteral("0001_initial"), QStringLiteral("0002_history"),
                                    QStringLiteral("0003_adjustment"), QStringLiteral("0004_photo_exported_hash"),
                                    QStringLiteral("0005_crop"), QStringLiteral("0006_layers"),
                                    QStringLiteral("0007_merge"), QStringLiteral("0008_remove_merge_output"),
                                    QStringLiteral("0009_merge_base_area")};
            for (int i = 1; i <= 9; ++i) {
                QFile f(QStringLiteral(":/photoeditor/schema/%1.sql").arg(i, 4, 10, QLatin1Char('0')));
                QVERIFY(f.open(QIODevice::ReadOnly));
                for (const QString &line : QString::fromUtf8(f.readAll()).split(u'\n')) {
                    if (!line.trimmed().isEmpty() && !line.startsWith(QLatin1String("--")))
                        QVERIFY2(q.exec(line), qPrintable(line));
                }
                q.exec(QStringLiteral("INSERT INTO django_migrations (app, name, applied) VALUES ('photoeditor', '%1', "
                                      "'2026-01-01')")
                           .arg(names[i - 1]));
            }
            raw.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("old"));
        Project project(root);
        QCOMPARE(project.db().schemaVersion(), Database::latestSchemaVersion());
        QVERIFY(project.db().scalar(QStringLiteral("SELECT count(*) FROM pragma_table_info('photoeditor_photo') WHERE "
                                                   "name = 'instagram_of_id'"))
                    .toInt()
                == 1);
    }

    void recentProjects()
    {
        QTemporaryDir dir;
        const QString root = makeProject(dir, 2);
        RecentProjects recent(AppPaths::appDb());
        recent.touch(root);
        recent.touch(root);
        QCOMPARE(recent.load().size(), size_t(1));
        const ProjectSummary s = RecentProjects::summary(recent.load().front());
        QCOMPARE(s.photoCount, 2);
        QVERIFY(s.coverPath.endsWith(QStringLiteral("IMG_0001.jpg")));
        recent.remove(root);
        QVERIFY(recent.load().empty());
    }

private:
    QTemporaryDir m_data;
};

QTEST_GUILESS_MAIN(CoreTest)
#include "tst_core.moc"
