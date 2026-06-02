#include "MainWindow.h"

#include "CallWindow.h"
#include "ChatHeader.h"
#include "ChatModel.h"
#include "ChatView.h"
#include "ContactsModel.h"
#include "FirstRunDialog.h"
#include "MessageHistory.h"
#include "MessageInputBar.h"
#include "SignalingClient.h"
#include "WebRtcSession.h"

#include <QListView>
#include <QMessageBox>
#include <QSplitter>
#include <QStatusBar>
#include <QStringList>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(tr("Signal"));
    resize(1100, 720);

    // --- Left pane: contacts list ----------------------------------------
    m_contactsModel = new ContactsModel(this);
    m_contactsView = new QListView;
    m_contactsView->setModel(m_contactsModel);
    m_contactsView->setObjectName(QStringLiteral("contactsView"));
    m_contactsView->setFocusPolicy(Qt::NoFocus);
    m_contactsView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_contactsView->setMinimumWidth(220);
    connect(m_contactsView, &QListView::clicked,
            this, &MainWindow::onContactSelected);

    // --- Right pane: header / chat / input -------------------------------
    m_chatHeader = new ChatHeader;
    m_chatView   = new ChatView;
    m_inputBar   = new MessageInputBar;
    m_chatModel  = new ChatModel(this);
    m_chatView->setChatModel(m_chatModel);

    auto *rightPane = new QWidget;
    auto *rightLayout = new QVBoxLayout(rightPane);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);
    rightLayout->addWidget(m_chatHeader);
    rightLayout->addWidget(m_chatView, 1);
    rightLayout->addWidget(m_inputBar);

    m_splitter = new QSplitter(Qt::Horizontal);
    m_splitter->addWidget(m_contactsView);
    m_splitter->addWidget(rightPane);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({260, 840});
    setCentralWidget(m_splitter);

    statusBar()->showMessage(tr("Не подключено"));

    connect(m_chatHeader, &ChatHeader::videoCallRequested,
            this, &MainWindow::onStartVideoCall);
    connect(m_chatHeader, &ChatHeader::audioCallRequested,
            this, &MainWindow::onStartAudioCall);
    connect(m_inputBar, &MessageInputBar::sendRequested,
            this, &MainWindow::onSendText);

    m_chatHeader->setContact(tr("Выберите контакт"), false);
    m_chatHeader->setCallEnabled(false);
    m_inputBar->setEnabled(false);
}

MainWindow::~MainWindow() = default;

void MainWindow::startDemo() {
    // Local-only demo path: no signaling, no GStreamer pipeline, no
    // network or camera permissions. Only the UI is exercised.
    m_config.userId      = QStringLiteral("you");
    m_config.displayName = QStringLiteral("Вы");

    QVector<Contact> contacts = {
        {QStringLiteral("grandpa"), QStringLiteral("Дедушка"), true},
        {QStringLiteral("mama"),    QStringLiteral("Мама"),    true},
        {QStringLiteral("papa"),    QStringLiteral("Папа"),    false},
        {QStringLiteral("sister"),  QStringLiteral("Сестра"),  true},
    };
    m_contactsModel->setContacts(contacts);
    m_chatModel->setSelfId(m_config.userId);

    // Pre-select the grandfather and pretend his conversation has some
    // history so the bubble delegate has something to render.
    m_currentContact = contacts[0];
    m_chatHeader->setContact(m_currentContact.displayName, true);
    m_chatHeader->setCallEnabled(true);
    m_inputBar->setEnabled(true);
    m_contactsView->setCurrentIndex(m_contactsModel->index(0));

    auto mk = [&](const QString &sender, const QString &text, int minutesAgo, bool delivered) {
        Message m;
        m.peerId    = QStringLiteral("grandpa");
        m.senderId  = sender;
        m.text      = text;
        m.sentAt    = QDateTime::currentDateTime().addSecs(-60 * minutesAgo);
        m.delivered = delivered;
        return m;
    };
    QVector<Message> demo = {
        mk(QStringLiteral("grandpa"), QStringLiteral("Здравствуй, внучок!"),                       45, true),
        mk(QStringLiteral("you"),     QStringLiteral("Привет, дедушка! Как ты?"),                 44, true),
        mk(QStringLiteral("grandpa"), QStringLiteral("Хорошо. Бабушка испекла пирог."),           42, true),
        mk(QStringLiteral("you"),     QStringLiteral("Вкуснотища! С чем?"),                       41, true),
        mk(QStringLiteral("grandpa"), QStringLiteral("С яблоками из сада. Приедешь — попробуешь."), 40, true),
        mk(QStringLiteral("you"),     QStringLiteral("Обязательно! На выходных созвонимся?"),     2,  false),
    };
    m_chatModel->setMessages(demo);

    statusBar()->showMessage(tr("Демо-режим — серверы не используются"));
}

bool MainWindow::ensureConfigured() {
    if (!m_config.exists()) {
        // First launch: the wizard fills m_config and writes config.json.
        FirstRunDialog dlg(&m_config, this);
        return dlg.exec() == QDialog::Accepted;
    }
    if (!m_config.load()) {
        QMessageBox::critical(this, tr("Конфигурация"), m_config.errorString());
        return false;
    }
    return true;
}

void MainWindow::start() {
    // Populate the contacts list from config. The 'online' flag stays false
    // until the first presence broadcast arrives from the signaling server.
    QVector<Contact> contacts;
    for (const auto &p : m_config.peers) {
        contacts.append({p.id, p.displayName, false});
    }
    m_contactsModel->setContacts(contacts);
    m_chatModel->setSelfId(m_config.userId);

    m_history = new MessageHistory(this);
    if (!m_history->open()) {
        QMessageBox::critical(this, tr("История"), m_history->errorString());
    }

    m_webrtc = new WebRtcSession(this);
    m_webrtc->setConfig(m_config);
    connect(m_webrtc, &WebRtcSession::localOfferReady,
            this, &MainWindow::onLocalOffer);
    connect(m_webrtc, &WebRtcSession::localAnswerReady,
            this, &MainWindow::onLocalAnswer);
    connect(m_webrtc, &WebRtcSession::localIceReady,
            this, &MainWindow::onLocalIce);
    connect(m_webrtc, &WebRtcSession::callConnected,
            this, &MainWindow::onCallConnected);
    connect(m_webrtc, &WebRtcSession::callEnded,
            this, &MainWindow::onCallEnded);
    connect(m_webrtc, &WebRtcSession::error,
            this, &MainWindow::onWebRtcError);
    connect(m_webrtc, &WebRtcSession::textReceived,
            this, &MainWindow::onTextReceived);
    connect(m_webrtc, &WebRtcSession::textDelivered,
            this, &MainWindow::onTextDelivered);

    m_signaling = new SignalingClient(this);
    connect(m_signaling, &SignalingClient::connected,
            this, &MainWindow::onSignalingConnected);
    connect(m_signaling, &SignalingClient::disconnected,
            this, &MainWindow::onSignalingDisconnected);
    connect(m_signaling, &SignalingClient::errorOccurred,
            this, &MainWindow::onSignalingError);
    connect(m_signaling, &SignalingClient::presenceChanged,
            this, &MainWindow::onPresenceChanged);
    connect(m_signaling, &SignalingClient::offerReceived,
            this, &MainWindow::onIncomingOffer);
    connect(m_signaling, &SignalingClient::answerReceived,
            this, &MainWindow::onIncomingAnswer);
    connect(m_signaling, &SignalingClient::iceReceived,
            this, &MainWindow::onIncomingIce);
    connect(m_signaling, &SignalingClient::byeReceived,
            this, &MainWindow::onIncomingBye);

    m_signaling->connectTo(m_config.signalingUrl, m_config.userId, m_config.signalingToken);
}

void MainWindow::onSignalingConnected() {
    statusBar()->showMessage(tr("Подключено"));
}
void MainWindow::onSignalingDisconnected() {
    statusBar()->showMessage(tr("Соединение разорвано, переподключаемся..."));
    m_contactsModel->setOnlinePeers({});
    if (!m_currentContact.id.isEmpty()) {
        m_chatHeader->setContact(m_currentContact.displayName, false);
        m_chatHeader->setCallEnabled(false);
    }
}
void MainWindow::onSignalingError(const QString &reason) {
    statusBar()->showMessage(tr("Ошибка сигналинга: %1").arg(reason));
}

void MainWindow::onPresenceChanged(const QStringList &online) {
    m_contactsModel->setOnlinePeers(online);
    if (!m_currentContact.id.isEmpty()) {
        const bool isOnline = online.contains(m_currentContact.id);
        m_currentContact.online = isOnline;
        m_chatHeader->setContact(m_currentContact.displayName, isOnline);
        m_chatHeader->setCallEnabled(isOnline);
    }
}

void MainWindow::onContactSelected(const QModelIndex &index) {
    const Contact c = m_contactsModel->contactAt(index.row());
    if (c.id.isEmpty()) return;
    m_currentContact = c;
    m_chatHeader->setContact(c.displayName, c.online);
    m_chatHeader->setCallEnabled(c.online);
    m_inputBar->setEnabled(true);
    if (m_history) {
        m_chatModel->setMessages(m_history->loadConversation(c.id));
    }
}

Contact MainWindow::currentContact() const { return m_currentContact; }

void MainWindow::selectContactById(const QString &id) {
    const int row = m_contactsModel->indexOf(id);
    if (row >= 0) onContactSelected(m_contactsModel->index(row));
}

void MainWindow::appendMessage(const QString &peerId, const QString &senderId, const QString &text) {
    Message m;
    m.peerId   = peerId;
    m.senderId = senderId;
    m.text     = text;
    m.sentAt   = QDateTime::currentDateTime();
    if (m_history) m_history->append(m);
    if (m_currentContact.id == peerId) m_chatModel->append(m);
}

// --- Chat send / receive -------------------------------------------------

void MainWindow::onSendText(const QString &text) {
    if (m_currentContact.id.isEmpty()) return;
    appendMessage(m_currentContact.id, m_config.userId, text);
    if (!m_webrtc) return;  // demo mode: UI-only, no data channel
    if (!m_webrtc->sendText(text)) {
        statusBar()->showMessage(tr("Канал данных ещё не открыт — позвоните, чтобы установить соединение"), 4000);
    }
}

void MainWindow::onTextReceived(const QString &fromPeerId, const QString &text) {
    appendMessage(fromPeerId, fromPeerId, text);
}

void MainWindow::onTextDelivered(const QString &text) {
    Q_UNUSED(text);
    // TODO: match delivered text back to a row id and call m_chatModel->markDelivered.
    // For now the bubble just keeps the single check-mark.
}

// --- Call lifecycle ------------------------------------------------------

void MainWindow::onStartVideoCall() { onStartAudioCall(); /* same path for now */ }

void MainWindow::onStartAudioCall() {
    if (m_currentContact.id.isEmpty() || !m_currentContact.online) return;
    if (!m_webrtc) {
        // Demo mode (startDemo) never creates the media/signaling stack.
        statusBar()->showMessage(tr("Демо-режим — звонки недоступны"), 4000);
        return;
    }
    if (!m_callWindow) {
        m_callWindow = new CallWindow();
        connect(m_callWindow, &CallWindow::hangupRequested,
                this, &MainWindow::onHangupRequested);
    }
    m_callWindow->setPeerName(m_currentContact.displayName);
    m_callWindow->setStatus(tr("Звоним..."));
    m_callWindow->show();
    m_webrtc->setVideoWindowHandle(m_callWindow->videoHandle());
    m_webrtc->start();
    m_webrtc->startCall(m_currentContact.id);
}

void MainWindow::onIncomingOffer(const QString &fromPeerId, const QString &sdp) {
    selectContactById(fromPeerId);
    if (!m_callWindow) {
        m_callWindow = new CallWindow();
        connect(m_callWindow, &CallWindow::hangupRequested,
                this, &MainWindow::onHangupRequested);
    }
    m_callWindow->setPeerName(m_currentContact.displayName);
    m_callWindow->setStatus(tr("Входящий звонок..."));
    m_callWindow->show();
    m_webrtc->setVideoWindowHandle(m_callWindow->videoHandle());
    m_webrtc->start();
    m_webrtc->acceptOffer(fromPeerId, sdp);
}

void MainWindow::onIncomingAnswer(const QString &fromPeerId, const QString &sdp) {
    Q_UNUSED(fromPeerId);
    m_webrtc->provideAnswer(sdp);
}

void MainWindow::onIncomingIce(const QString &fromPeerId, const QString &candidate,
                                const QString &sdpMid, int sdpMLineIndex) {
    Q_UNUSED(fromPeerId);
    Q_UNUSED(sdpMid);
    m_webrtc->addRemoteIce(candidate, sdpMLineIndex);
}

void MainWindow::onIncomingBye(const QString &fromPeerId) {
    Q_UNUSED(fromPeerId);
    m_webrtc->hangup();
    if (m_callWindow) m_callWindow->hide();
}

void MainWindow::onLocalOffer(const QString &peerId, const QString &sdp) {
    m_signaling->sendOffer(peerId, sdp);
}
void MainWindow::onLocalAnswer(const QString &peerId, const QString &sdp) {
    m_signaling->sendAnswer(peerId, sdp);
}
void MainWindow::onLocalIce(const QString &peerId, const QString &candidate,
                             const QString &sdpMid, int sdpMLineIndex) {
    m_signaling->sendIce(peerId, candidate, sdpMid, sdpMLineIndex);
}

void MainWindow::onCallConnected() {
    if (m_callWindow) m_callWindow->setStatus(tr("В разговоре"));
}

void MainWindow::onCallEnded() {
    if (m_callWindow) m_callWindow->hide();
}

void MainWindow::onWebRtcError(const QString &message) {
    statusBar()->showMessage(tr("Медиа: %1").arg(message), 5000);
}

void MainWindow::onHangupRequested() {
    if (!m_currentContact.id.isEmpty()) m_signaling->sendBye(m_currentContact.id);
    m_webrtc->hangup();
    if (m_callWindow) m_callWindow->hide();
}
