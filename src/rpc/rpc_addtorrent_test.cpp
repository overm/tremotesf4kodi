// SPDX-FileCopyrightText: 2026 Tremotesf contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>

#include <QFile>
#include <QCryptographicHash>
#include <QDir>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

#include "rpc.h"

using namespace Qt::StringLiterals;
using namespace tremotesf;

namespace {
    QByteArray metainfo(const QByteArray& name, bool multipleFiles = false) {
        const auto files =
            multipleFiles ? "5:filesld6:lengthi1e4:pathl5:a.txteed6:lengthi1e4:pathl5:b.txteee"_ba : "6:lengthi1e"_ba;
        return "d4:infod"_ba + files + "4:name"_ba + QByteArray::number(name.size()) + ':' + name + "ee"_ba;
    }
}

// Exercise the public RPC API, including background parsing and the actual HTTP
// request. The server only listens on loopback and never downloads torrent data.
class RpcAddTorrentTest final : public QObject {
    Q_OBJECT

private:
    std::unique_ptr<QTcpServer> mServer;
    std::unique_ptr<Rpc> mRpc;
    QList<QJsonObject> mAddRequests;
    int mRpcVersion{17};

    void configureRpc(int version, quint16 port = 0) {
        mRpcVersion = version;
        mRpc = std::make_unique<Rpc>();
        ConnectionConfiguration configuration{};
        configuration.address = "127.0.0.1"_L1;
        configuration.port = port ? port : mServer->serverPort();
        configuration.apiPath = "/transmission/rpc"_L1;
        configuration.proxyType = ConnectionConfiguration::ProxyType::None;
        configuration.timeout = 5;
        configuration.updateInterval = 3600;
        mRpc->setConnectionConfiguration(configuration);
        mRpc->connect();
    }

    void respond(QTcpSocket* socket, const QByteArray& body) {
        const auto request = QJsonDocument::fromJson(body).object();
        const auto method = request.value("method"_L1).toString();
        QJsonObject arguments;
        if (method == "session-get"_L1) {
            arguments = {
                {u"rpc-version"_s, mRpcVersion},
                {u"rpc-version-minimum"_s, 14},
                {u"download-dir"_s, "/downloads"_L1},
                {u"config-dir"_s, "/config"_L1}
            };
        } else if (method == "torrent-get"_L1) {
            arguments.insert("torrents"_L1, QJsonArray{});
        } else if (method == "torrent-add"_L1) {
            mAddRequests.append(request.value("arguments"_L1).toObject());
            arguments.insert("torrent-added"_L1, QJsonObject{{u"id"_s, 1}});
        }
        const auto response = QJsonDocument(
                                  QJsonObject{{u"result"_s, "success"_L1}, {u"arguments"_s, arguments}}
        ).toJson(QJsonDocument::Compact);
        socket->write(
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "_ba
            + QByteArray::number(response.size())
            + "\r\n\r\n"_ba
            + response
        );
        socket->disconnectFromHost();
    }

private slots:
    void init() {
        mAddRequests.clear();
        mServer = std::make_unique<QTcpServer>();
        QVERIFY(mServer->listen(QHostAddress::LocalHost));
        QObject::connect(mServer.get(), &QTcpServer::newConnection, this, [this] {
            while (mServer->hasPendingConnections()) {
                auto* const socket = mServer->nextPendingConnection();
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                QObject::connect(
                    socket,
                    &QTcpSocket::readyRead,
                    socket,
                    [this, socket, buffer = QByteArray{}]() mutable {
                        buffer += socket->readAll();
                        const auto headerEnd = buffer.indexOf("\r\n\r\n"_ba);
                        if (headerEnd == -1) return;
                        qsizetype contentLength{-1};
                        for (const auto& header : buffer.first(headerEnd).split('\n')) {
                            if (header.toLower().startsWith("content-length:"_ba)) {
                                contentLength = header.sliced(header.indexOf(':') + 1).trimmed().toLongLong();
                            }
                        }
                        if (contentLength < 0 || buffer.size() < headerEnd + 4 + contentLength) return;
                        respond(socket, buffer.sliced(headerEnd + 4, contentLength));
                        buffer.clear();
                    }
                );
            }
        });
    }

    void cleanup() {
        if (mRpc) mRpc->disconnect();
        mRpc.reset();
        mServer.reset();
    }

    void addTorrentFile_data() {
        QTest::addColumn<int>("rpcVersion");
        QTest::addColumn<QByteArray>("torrentData");
        QTest::addColumn<QString>("downloadDirectory");
        QTest::addColumn<bool>("separateDirectory");
        QTest::addColumn<QString>("expectedDirectory");
        QTest::addColumn<bool>("accepted");

        const auto addCase = [](const char* name,
                                const QByteArray& data,
                                const QString& directory,
                                bool separate,
                                const QString& expected,
                                bool accepted = true) {
            for (int version : {14, 16, 17}) {
                const auto row = QByteArray(name) + "-rpc"_ba + QByteArray::number(version);
                QTest::newRow(row.constData()) << version << data << directory << separate << expected << accepted;
            }
        };
        const auto single = metainfo("movie.mkv"_ba);
        const auto multiple = metainfo("collection"_ba, true);
        addCase("single-disabled", single, "/downloads"_L1, false, "/downloads"_L1);
        addCase("single-enabled", single, "/downloads"_L1, true, "/downloads/movie.mkv"_L1);
        addCase("trailing-slash", single, "/downloads/"_L1, true, "/downloads/movie.mkv"_L1);
        addCase("unix-root", single, "/"_L1, true, "/movie.mkv"_L1);
        addCase("windows-path", single, "C:/downloads"_L1, true, "C:/downloads/movie.mkv"_L1);
        addCase("windows-backslash", single, "C:\\downloads\\"_L1, true, "C:\\downloads\\movie.mkv"_L1);
        addCase("multiple-enabled", multiple, "/downloads"_L1, true, "/downloads"_L1);
        addCase("multiple-disabled", multiple, "/downloads"_L1, false, "/downloads"_L1);
        addCase(
            "unicode",
            metainfo(QString::fromUtf8("Фильм.mkv").toUtf8()),
            "/downloads"_L1,
            true,
            QString::fromUtf8("/downloads/Фильм.mkv")
        );
        for (const auto& name : {""_ba, "."_ba, ".."_ba, "../escape"_ba, "a/b"_ba, "a\\b"_ba}) {
            const auto row = "invalid-name-"_ba + name.toHex();
            addCase(row.constData(), metainfo(name), "/downloads"_L1, true, {}, false);
        }
        addCase("malformed-metainfo", "not bencode"_ba, "/downloads"_L1, true, {}, false);
    }

    void addTorrentFile() {
        QFETCH(int, rpcVersion);
        QFETCH(QByteArray, torrentData);
        QFETCH(QString, downloadDirectory);
        QFETCH(bool, separateDirectory);
        QFETCH(QString, expectedDirectory);
        QFETCH(bool, accepted);

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("test.torrent"_L1);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(torrentData), torrentData.size());
        file.close();

        configureRpc(rpcVersion);
        QTRY_VERIFY_WITH_TIMEOUT(mRpc->isConnected(), 5000);
        QSignalSpy errors(mRpc.get(), &Rpc::torrentAddError);
        QSignalSpy updated(mRpc.get(), &Rpc::torrentsUpdated);
        mRpc->addTorrentFile(
            path,
            downloadDirectory,
            {0},
            {1},
            {2},
            {},
            TorrentData::Priority::High,
            false,
            separateDirectory,
            Rpc::DeleteFileMode::No,
            {"test-label"_L1}
        );

        if (accepted) {
            QTRY_COMPARE_WITH_TIMEOUT(mAddRequests.size(), 1, 5000);
            QTRY_VERIFY_WITH_TIMEOUT(!updated.isEmpty(), 5000);
            QCOMPARE(errors.size(), 0);
            const auto arguments = mAddRequests.first();
            QCOMPARE(arguments.value("download-dir"_L1).toString(), expectedDirectory);
            QCOMPARE(QByteArray::fromBase64(arguments.value("metainfo"_L1).toString().toLatin1()), torrentData);
            QCOMPARE(arguments.value("paused"_L1).toBool(), true);
            QCOMPARE(arguments.value("bandwidthPriority"_L1).toInt(), 1);
            QCOMPARE(arguments.value("files-unwanted"_L1).toArray(), QJsonArray{0});
            QCOMPARE(arguments.value("priority-high"_L1).toArray(), QJsonArray{1});
            QCOMPARE(arguments.value("priority-low"_L1).toArray(), QJsonArray{2});
            QCOMPARE(arguments.value("labels"_L1).toArray(), QJsonArray{"test-label"_L1});
        } else {
            QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 1, 5000);
            QCOMPARE(errors.first().first().toString(), path);
            QVERIFY(mAddRequests.isEmpty());
        }
        QVERIFY(QFile::exists(path));
        QVERIFY(mRpc->isConnected());
    }

    void missingTorrentFile() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        configureRpc(17);
        QTRY_VERIFY_WITH_TIMEOUT(mRpc->isConnected(), 5000);
        QSignalSpy errors(mRpc.get(), &Rpc::torrentAddError);
        const auto path = directory.filePath("missing.torrent"_L1);
        mRpc->addTorrentFile(
            path,
            "/downloads"_L1,
            {},
            {},
            {},
            {},
            TorrentData::Priority::Normal,
            true,
            true,
            Rpc::DeleteFileMode::No,
            {}
        );
        QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 1, 5000);
        QVERIFY(mAddRequests.isEmpty());
        QVERIFY(mRpc->isConnected());
    }

    void realTransmission_data() {
        QTest::addColumn<bool>("multipleFiles");
        QTest::addColumn<bool>("separateDirectory");
        QTest::newRow("single-disabled") << false << false;
        QTest::newRow("single-enabled") << false << true;
        QTest::newRow("multiple-disabled") << true << false;
        QTest::newRow("multiple-enabled") << true << true;
    }

    void realTransmission() {
        const auto executable = QStandardPaths::findExecutable("transmission-daemon"_L1);
        if (executable.isEmpty()) QSKIP("Optional integration test requires transmission-daemon on PATH");
        QFETCH(bool, multipleFiles);
        QFETCH(bool, separateDirectory);

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto downloadDirectory = directory.filePath("downloads"_L1);
        QVERIFY(QDir().mkpath(downloadDirectory));
        const auto port = mServer->serverPort();
        mServer->close();
        const QJsonObject settings{
            {u"rpc-enabled"_s, true},
            {u"rpc-port"_s, port},
            {u"rpc-bind-address"_s, "127.0.0.1"_L1},
            {u"rpc-authentication-required"_s, false},
            {u"rpc-whitelist-enabled"_s, true},
            {u"rpc-whitelist"_s, "127.0.0.1"_L1},
            {u"bind-address-ipv4"_s, "127.0.0.1"_L1},
            {u"bind-address-ipv6"_s, "::1"_L1},
            {u"dht-enabled"_s, false},
            {u"pex-enabled"_s, false},
            {u"lpd-enabled"_s, false},
            {u"port-forwarding-enabled"_s, false},
            {u"utp-enabled"_s, false},
            {u"peer-port"_s, 0},
            {u"download-dir"_s, downloadDirectory}
        };
        QFile settingsFile(directory.filePath("settings.json"_L1));
        QVERIFY(settingsFile.open(QIODevice::WriteOnly));
        const auto settingsData = QJsonDocument(settings).toJson();
        QCOMPARE(settingsFile.write(settingsData), settingsData.size());
        settingsFile.close();

        QProcess daemon;
        daemon.setProcessChannelMode(QProcess::MergedChannels);
        const auto stopDaemon = qScopeGuard([&] {
            if (mRpc) mRpc->disconnect();
            if (daemon.state() == QProcess::NotRunning) return;
            daemon.terminate();
            if (!daemon.waitForFinished(5000)) {
                daemon.kill();
                daemon.waitForFinished(5000);
            }
        });
        daemon.start(executable, {"--foreground"_L1, "--config-dir"_L1, directory.path()});
        QVERIFY2(daemon.waitForStarted(5000), qPrintable(daemon.errorString()));
        const auto ready = [&] {
            QTcpSocket socket;
            socket.connectToHost(QHostAddress::LocalHost, port);
            return socket.waitForConnected(50);
        };
        QTRY_VERIFY_WITH_TIMEOUT(ready(), 10000);

        configureRpc(17, port);
        QTRY_VERIFY_WITH_TIMEOUT(mRpc->isConnected(), 10000);

        auto torrentData = metainfo("payload"_ba, multipleFiles);
        torrentData.chop(2);
        torrentData += "12:piece lengthi16384e6:pieces20:"_ba;
        torrentData += QCryptographicHash::hash(multipleFiles ? "xx"_ba : "x"_ba, QCryptographicHash::Sha1);
        torrentData += "ee"_ba;
        const auto path = directory.filePath("test.torrent"_L1);
        QFile torrentFile(path);
        QVERIFY(torrentFile.open(QIODevice::WriteOnly));
        QCOMPARE(torrentFile.write(torrentData), torrentData.size());
        torrentFile.close();

        QSignalSpy errors(mRpc.get(), &Rpc::torrentAddError);
        mRpc->addTorrentFile(
            path,
            downloadDirectory,
            {},
            {},
            {},
            {},
            TorrentData::Priority::Normal,
            false,
            separateDirectory,
            Rpc::DeleteFileMode::No,
            {}
        );
        QTRY_VERIFY_WITH_TIMEOUT(!mRpc->torrents().empty() || !errors.isEmpty(), 10000);
        QCOMPARE(errors.size(), 0);
        QCOMPARE(mRpc->torrents().size(), 1);
        const auto& torrent = mRpc->torrents().front()->data();
        const auto expectedDirectory =
            separateDirectory && !multipleFiles ? downloadDirectory + "/payload"_L1 : downloadDirectory;
        QCOMPARE(QDir::fromNativeSeparators(torrent.downloadDirectory), expectedDirectory);
        QCOMPARE(torrent.status, TorrentData::Status::Paused);
        QVERIFY(QFile::exists(path));
        mRpc->shutdownServer();
        QTRY_COMPARE_WITH_TIMEOUT(daemon.state(), QProcess::NotRunning, 10000);
        QCOMPARE(daemon.exitStatus(), QProcess::NormalExit);
        QCOMPARE(daemon.exitCode(), 0);
    }
};

QTEST_GUILESS_MAIN(RpcAddTorrentTest)

#include "rpc_addtorrent_test.moc"
