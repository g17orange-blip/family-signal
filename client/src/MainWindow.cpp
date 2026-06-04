#include "MainWindow.h"

#include "CallWindow.h"
#include "ChatHeader.h"
#include "ChatModel.h"
#include "ChatView.h"
#include "ContactItemDelegate.h"
#include "ContactsModel.h"
#include "FirstRunDialog.h"
#include "MessageHistory.h"
#include "MessageInputBar.h"
#include "SettingsDialog.h"
#include "SignalingClient.h"
#include "WebRtcSession.h"

#include <QLabel>
#include <QListView>
#include <QMenuBar>
#include <QMessageBox>
#include <QSplitter>
#include <QStackedWidget>
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
    m_contactsView->setItemDelegate(new ContactItemDelegate(m_contactsView));
    connect(m_contactsView, &QListView::clicked,
            this, &MainWindow::onContactSelected);

    // --- Right pane: header / chat / input -------------------------------
    m_chatHeader = new ChatHeader;
    m_chatView   = new ChatView;
    m_inputBar   = new MessageInputBar;
    m_chatModel  = new ChatModel(this);
    m_chatView->setChatModel(m_chatModel);

    auto *chatPane = new QWidget;
    auto *chatLayout = new QVBoxLayout(chatPane);
    chatLayout->setContentsMargins(0, 0, 0, 0);
    chatLayout->setSpacing(0);
    chatLayout->addWidget(m_chatHeader);
    chatLayout->addWidget(m_chatView, 1);
    chatLayout->addWidget(m_inputBar);

    // Until a contact is picked the right side shows only a hint — no call
    // buttons, no input field. Otherwise the grandfather presses "call"
    // before choosing whom to call and nothing (visibly) happens.
    auto *placeholder = new QLabel(
        tr("← Выберите слева,\nс кем поговорить"));
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setObjectName(QStringLiteral("noChatPlaceholder"));
    placeholder->setStyleSheet(
        QStringLiteral("font-size: 22px; color: #8a9aa5;"));

    m_rightStack = new QStackedWidget;
    m_rightStack->addWidget(placeholder);   // index 0 — nothing selected
    m_rightStack->addWidget(chatPane);      // index 1 — conversation
    m_rightStack->setCurrentIndex(0);

    m_splitter = new QSplitter(Qt::Horizontal);
    m_splitter->addWidget(m_contactsView);
    m_splitter->addWidget(m_rightStack);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({260, 840});
    setCentralWidget(m_splitter);

    statusBar()->showMessage(tr("Не подключено"));

    // Settings hide behind a menu on purpose — nothing destructive sits on
    // the main UI where it could be pressed by accident.
    auto *appMenu = menuBar()->addMenu(tr("Меню"));
    appMenu->addAction(tr("Настройки…"), this, &MainWindow::onOpenSettings);

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
    m_rightStack->setCurrentIndex(1);
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
    connect(m_webrtc, &WebRtcSession::channelOpen, this, [this] {
        // A call's DataChannel also delivers queued text.
        if (!m_webrtc->remotePeerId().isEmpty())
            flushQueuedMessages(m_webrtc->remotePeerId());
    });

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
    // Keep silent text-delivery sessions in step with who's online.
    syncChatSessions(online);
}

void MainWindow::onContactSelected(const QModelIndex &index) {
    const Contact c = m_contactsModel->contactAt(index.row());
    if (c.id.isEmpty()) return;
    m_currentContact = c;
    m_rightStack->setCurrentIndex(1);   // swap the placeholder for the chat
    m_contactsModel->clearUnread(c.id);
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

Message MainWindow::appendMessage(const QString &peerId, const QString &senderId,
                                  const QString &text, const QString &msgId) {
    Message m;
    m.msgId    = msgId;
    m.peerId   = peerId;
    m.senderId = senderId;
    m.text     = text;
    m.sentAt   = QDateTime::currentDateTime();
    // Incoming messages are "delivered" by definition; only our own
    // outgoing ones wait for the peer's ack.
    m.delivered = (senderId != m_config.userId);
    if (m_history) m_history->append(m);   // fills m.msgId/rowId if empty
    if (m_currentContact.id == peerId) m_chatModel->append(m);
    return m;
}

// --- Chat send / receive -------------------------------------------------

void MainWindow::onSendText(const QString &text) {
    if (m_currentContact.id.isEmpty()) return;
    const Message m = appendMessage(m_currentContact.id, m_config.userId, text);
    if (!m_webrtc) return;  // demo mode: UI-only, no data channel
    if (!sendViaAnyChannel(m.peerId, m.msgId, m.text)) {
        statusBar()->showMessage(
            tr("Собеседник не в сети — сообщение будет доставлено автоматически"), 4000);
    }
}

bool MainWindow::sendViaAnyChannel(const QString &peerId, const QString &msgId,
                                   const QString &text) {
    // Prefer the silent chat session; fall back to an active call's channel.
    if (WebRtcSession *s = m_chatSessions.value(peerId)) {
        if (s->isChannelOpen() && s->sendText(msgId, text)) return true;
    }
    if (m_webrtc && m_webrtc->remotePeerId() == peerId &&
        m_webrtc->isChannelOpen() && m_webrtc->sendText(msgId, text))
        return true;
    return false;
}

void MainWindow::flushQueuedMessages(const QString &peerId) {
    if (!m_history) return;
    const auto queued = m_history->loadUndelivered(peerId, m_config.userId);
    for (const Message &m : queued) {
        if (!sendViaAnyChannel(peerId, m.msgId, m.text)) break;
    }
    if (!queued.isEmpty())
        statusBar()->showMessage(tr("Отправляем недоставленные сообщения…"), 3000);
}

void MainWindow::onTextReceived(const QString &fromPeerId, const QString &msgId,
                                const QString &text) {
    // The sender re-sends anything unacked (e.g. our ack got lost), so
    // duplicates are expected — drop them by msg_id.
    if (m_history && m_history->containsMsgId(msgId)) return;
    appendMessage(fromPeerId, fromPeerId, text, msgId);
    // Red badge for conversations that aren't on screen right now.
    if (m_currentContact.id != fromPeerId)
        m_contactsModel->incrementUnread(fromPeerId);
}

void MainWindow::onTextDelivered(const QString &msgId) {
    if (!m_history || !m_history->markDeliveredByMsgId(msgId)) return;
    // Refresh the visible conversation so the bubble gets its check-mark.
    if (!m_currentContact.id.isEmpty())
        m_chatModel->setMessages(m_history->loadConversation(m_currentContact.id));
}

// --- Background chat sessions ---------------------------------------------

WebRtcSession *MainWindow::ensureChatSession(const QString &peerId) {
    if (WebRtcSession *existing = m_chatSessions.value(peerId)) return existing;

    auto *s = new WebRtcSession(this, WebRtcSession::Mode::Chat);
    s->setConfig(m_config);
    const QString kind = QStringLiteral("chat");
    connect(s, &WebRtcSession::localOfferReady, this,
            [this, kind](const QString &peer, const QString &sdp) {
                m_signaling->sendOffer(peer, sdp, kind);
            });
    connect(s, &WebRtcSession::localAnswerReady, this,
            [this, kind](const QString &peer, const QString &sdp) {
                m_signaling->sendAnswer(peer, sdp, kind);
            });
    connect(s, &WebRtcSession::localIceReady, this,
            [this, kind](const QString &peer, const QString &cand,
                         const QString &mid, int mline) {
                m_signaling->sendIce(peer, cand, mid, mline, kind);
            });
    connect(s, &WebRtcSession::textReceived,  this, &MainWindow::onTextReceived);
    connect(s, &WebRtcSession::textDelivered, this, &MainWindow::onTextDelivered);
    connect(s, &WebRtcSession::channelOpen, this, [this, peerId] {
        qInfo() << "[chat] channel OPEN to" << peerId;
        flushQueuedMessages(peerId);
    });
    connect(s, &WebRtcSession::error, this, [peerId](const QString &e) {
        qWarning() << "[chat] session error" << peerId << ":" << e;
    });

    s->start();
    m_chatSessions.insert(peerId, s);
    return s;
}

void MainWindow::syncChatSessions(const QStringList &onlinePeers) {
    if (!m_signaling) return;   // demo mode
    qInfo() << "[chat] presence:" << onlinePeers << "self:" << m_config.userId
            << "sessions:" << m_chatSessions.keys();

    // Tear down sessions to peers that went offline.
    for (auto it = m_chatSessions.begin(); it != m_chatSessions.end();) {
        if (!onlinePeers.contains(it.key())) {
            it.value()->stop();
            it.value()->deleteLater();
            it = m_chatSessions.erase(it);
        } else {
            ++it;
        }
    }
    // Open sessions to peers that came online. Deterministic initiator
    // (smaller userId offers) prevents both sides from offering at once;
    // the other side just answers our offer.
    for (const QString &peer : onlinePeers) {
        if (m_config.userId < peer && !m_chatSessions.contains(peer)) {
            qInfo() << "[chat] initiating silent session to" << peer;
            ensureChatSession(peer)->startCall(peer);   // chat mode: data only
        }
    }
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

void MainWindow::onIncomingOffer(const QString &fromPeerId, const QString &sdp,
                                 const QString &kind) {
    if (kind == QLatin1String("chat")) {
        // Silent text-delivery session — no ringing, no call window.
        ensureChatSession(fromPeerId)->acceptOffer(fromPeerId, sdp);
        return;
    }
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

void MainWindow::onIncomingAnswer(const QString &fromPeerId, const QString &sdp,
                                  const QString &kind) {
    if (kind == QLatin1String("chat")) {
        if (WebRtcSession *s = m_chatSessions.value(fromPeerId)) s->provideAnswer(sdp);
        return;
    }
    Q_UNUSED(fromPeerId);
    m_webrtc->provideAnswer(sdp);
}

void MainWindow::onIncomingIce(const QString &fromPeerId, const QString &candidate,
                                const QString &sdpMid, int sdpMLineIndex,
                                const QString &kind) {
    Q_UNUSED(sdpMid);
    if (kind == QLatin1String("chat")) {
        if (WebRtcSession *s = m_chatSessions.value(fromPeerId))
            s->addRemoteIce(candidate, sdpMLineIndex);
        return;
    }
    Q_UNUSED(fromPeerId);
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

void MainWindow::onOpenSettings() {
    SettingsDialog dlg(&m_config, m_history, this);
    connect(&dlg, &SettingsDialog::historyCleared, this, [this] {
        // The store is empty now; drop what's on screen too.
        m_chatModel->setMessages({});
        if (!m_currentContact.id.isEmpty() && m_history)
            m_chatModel->setMessages(m_history->loadConversation(m_currentContact.id));
    });
    dlg.exec();
}
