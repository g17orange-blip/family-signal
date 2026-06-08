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
#include "Ringtone.h"
#include "SettingsDialog.h"
#include "SignalingClient.h"
#include "WebRtcSession.h"

#include <QLabel>
#include <QListView>
#include <QMenuBar>
#include <QMessageBox>
#include <QScrollBar>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(tr("Signal"));
    resize(1100, 720);

    m_ringtone = new Ringtone(this);

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
    connect(m_chatHeader, &ChatHeader::hangupRequested,
            this, &MainWindow::onHangupRequested);
    connect(m_inputBar, &MessageInputBar::sendRequested,
            this, &MainWindow::onSendText);
    connect(m_chatView, &ChatView::needOlderMessages,
            this, &MainWindow::loadOlderMessages);

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

    auto mk = [&](const QString &sender, const QString &text, int minutesAgo,
                  bool delivered, bool read = false) {
        Message m;
        m.peerId    = QStringLiteral("grandpa");
        m.senderId  = sender;
        m.text      = text;
        m.sentAt    = QDateTime::currentDateTime().addSecs(-60 * minutesAgo);
        m.delivered = delivered;
        m.read      = read;
        return m;
    };
    QVector<Message> demo = {
        mk(QStringLiteral("grandpa"), QStringLiteral("Здравствуй, внучок!"),                       45, true,  true),
        mk(QStringLiteral("you"),     QStringLiteral("Привет, дедушка! Как ты?"),                 44, true,  true),
        mk(QStringLiteral("grandpa"), QStringLiteral("Хорошо. Бабушка испекла пирог."),           42, true,  true),
        mk(QStringLiteral("you"),     QStringLiteral("Вкуснотища! С чем?"),                       41, true,  true),
        mk(QStringLiteral("grandpa"), QStringLiteral("С яблоками из сада. Приедешь — попробуешь."), 40, true, true),
        mk(QStringLiteral("you"),     QStringLiteral("Обязательно! На выходных созвонимся?"),     5,  true),
        mk(QStringLiteral("you"),     QStringLiteral("Я пока в дороге, наберу как доеду"),        2,  false),
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
    connect(m_webrtc, &WebRtcSession::connectionInterrupted,
            this, &MainWindow::onCallConnectionInterrupted);
    connect(m_webrtc, &WebRtcSession::connectionRestored,
            this, &MainWindow::onCallConnectionRestored);
    connect(m_webrtc, &WebRtcSession::connectionFailed,
            this, &MainWindow::onCallConnectionFailed);
    connect(m_webrtc, &WebRtcSession::textReceived,
            this, &MainWindow::onTextReceived);
    connect(m_webrtc, &WebRtcSession::textDelivered,
            this, &MainWindow::onTextDelivered);
    connect(m_webrtc, &WebRtcSession::peerReadMessages,
            this, &MainWindow::onPeerReadMessages);
    // Emitted from a GStreamer streaming thread — the cross-thread connect
    // is queued automatically, painting happens on the UI thread.
    connect(m_webrtc, &WebRtcSession::selfFrame, this, [this](const QImage &f) {
        if (m_callWindow && m_callWindow->isVisible())
            m_callWindow->setSelfFrame(f);
    });
    connect(m_webrtc, &WebRtcSession::remoteFrame, this, [this](const QImage &f) {
        if (m_callWindow && m_callWindow->isVisible())
            m_callWindow->setRemoteFrame(f);
    });
    connect(m_webrtc, &WebRtcSession::channelOpen, this, [this] {
        // A call's DataChannel also delivers queued text and receipts.
        if (!m_webrtc->remotePeerId().isEmpty()) {
            flushQueuedMessages(m_webrtc->remotePeerId());
            sendReadReceipts(m_webrtc->remotePeerId(), /*markConversation=*/false);
        }
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
    m_olderExhausted = false;
    m_loadingOlder = false;
    if (m_history) {
        m_chatModel->setMessages(m_history->loadConversation(c.id));
    }
    sendReadReceipts(c.id, /*markConversation=*/true);
}

void MainWindow::loadOlderMessages() {
    if (!m_history || m_loadingOlder || m_olderExhausted ||
        m_currentContact.id.isEmpty())
        return;
    const qint64 before = m_chatModel->firstRowId();
    if (before < 0) return;
    m_loadingOlder = true;

    const auto older = m_history->loadConversation(m_currentContact.id, 50, before);
    if (older.isEmpty()) {
        m_olderExhausted = true;
        m_loadingOlder = false;
        return;
    }
    // Keep the viewport anchored on the message the user was looking at:
    // remember the distance from the bottom and restore it after the rows
    // are prepended and laid out.
    QScrollBar *bar = m_chatView->verticalScrollBar();
    const int fromBottom = bar->maximum() - bar->value();
    m_chatModel->prependMessages(older);
    QMetaObject::invokeMethod(this, [this, fromBottom] {
        QScrollBar *bar = m_chatView->verticalScrollBar();
        bar->setValue(bar->maximum() - fromBottom);
        m_loadingOlder = false;
    }, Qt::QueuedConnection);
}

Contact MainWindow::currentContact() const { return m_currentContact; }

void MainWindow::selectContactById(const QString &id) {
    const int row = m_contactsModel->indexOf(id);
    if (row >= 0) onContactSelected(m_contactsModel->index(row));
}

void MainWindow::logCallEvent(const QString &peerId, const QString &text, bool bad) {
    Message m;
    m.kind      = bad ? Message::CallEventBad : Message::CallEventGood;
    m.peerId    = peerId;
    m.senderId  = m_config.userId;   // local note; never leaves this device
    m.text      = text;
    m.sentAt    = QDateTime::currentDateTime();
    m.delivered = true;   // keeps it out of the offline queue
    m.read      = true;
    if (m_history) m_history->append(m);
    if (m_currentContact.id == peerId) m_chatModel->append(m);
    // The red badge is for calls that need the user's attention: missed,
    // declined, dropped. A call that actually took place was "read" by
    // definition — its duration pill must not light the contact up.
    else if (bad) m_contactsModel->incrementUnread(peerId);
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

WebRtcSession *MainWindow::openChannelTo(const QString &peerId) const {
    // Prefer the silent chat session; fall back to an active call's channel.
    if (WebRtcSession *s = m_chatSessions.value(peerId))
        if (s->isChannelOpen()) return s;
    if (m_webrtc && m_webrtc->remotePeerId() == peerId && m_webrtc->isChannelOpen())
        return m_webrtc;
    return nullptr;
}

bool MainWindow::sendViaAnyChannel(const QString &peerId, const QString &msgId,
                                   const QString &text) {
    WebRtcSession *s = openChannelTo(peerId);
    return s && s->sendText(msgId, text);
}

void MainWindow::sendReadReceipts(const QString &peerId, bool markConversation) {
    if (!m_history || peerId.isEmpty()) return;
    const QStringList ids = markConversation
        ? m_history->markConversationRead(peerId, m_config.userId)
        : m_history->pendingReceipts(peerId, m_config.userId);
    if (ids.isEmpty()) return;
    if (WebRtcSession *s = openChannelTo(peerId)) {
        if (s->sendReadReceipts(ids)) m_history->markReceiptsSent(ids);
    }
    // If no channel is open the receipts stay pending and go out on the
    // next channelOpen for this peer.
}

void MainWindow::onPeerReadMessages(const QStringList &msgIds) {
    if (!m_history || !m_history->markPeerRead(msgIds)) return;
    if (!m_currentContact.id.isEmpty())
        m_chatModel->setMessages(m_history->loadConversation(m_currentContact.id));
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
    if (m_currentContact.id == fromPeerId) {
        // On screen right now — the peer gets the read receipt immediately.
        sendReadReceipts(fromPeerId, /*markConversation=*/true);
    } else {
        // Red badge for conversations that aren't on screen.
        m_contactsModel->incrementUnread(fromPeerId);
    }
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
    connect(s, &WebRtcSession::peerReadMessages,
            this, &MainWindow::onPeerReadMessages);
    connect(s, &WebRtcSession::channelOpen, this, [this, peerId] {
        qInfo() << "[chat] channel OPEN to" << peerId;
        flushQueuedMessages(peerId);
        sendReadReceipts(peerId, /*markConversation=*/false);   // backlog receipts
    });
    connect(s, &WebRtcSession::error, this, [peerId](const QString &e) {
        qWarning() << "[chat] session error" << peerId << ":" << e;
    });
    // ICE died (network outage outlived the grace timer): drop the zombie
    // session. Presence alone wouldn't catch this — the peer-to-peer path
    // can break while both sides still reach the signaling server. The
    // deterministic initiator re-offers; the other side waits for it.
    connect(s, &WebRtcSession::connectionFailed, this, [this, peerId] {
        qWarning() << "[chat] ICE failed to" << peerId << "— rebuilding session";
        if (WebRtcSession *dead = m_chatSessions.take(peerId)) {
            dead->stop();
            dead->deleteLater();
        }
        if (m_config.userId < peerId) {
            QTimer::singleShot(2000, this, [this, peerId] {
                const int row = m_contactsModel->indexOf(peerId);
                if (row < 0 || !m_contactsModel->contactAt(row).online) return;
                if (m_chatSessions.contains(peerId)) return;
                ensureChatSession(peerId)->startCall(peerId);
            });
        }
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

CallWindow *MainWindow::ensureCallWindow() {
    if (!m_callWindow) {
        // Parented to the main window (the Qt::Window flag in CallWindow
        // keeps it a separate top-level window): on macOS an orphaned
        // secondary window can vanish behind other apps with no way back —
        // ownership ties it to the app so it returns on reactivation.
        m_callWindow = new CallWindow(this);
        connect(m_callWindow, &CallWindow::acceptRequested,
                this, &MainWindow::onAcceptIncomingCall);
        connect(m_callWindow, &CallWindow::hangupRequested,
                this, &MainWindow::onHangupRequested);
    }
    return m_callWindow;
}

void MainWindow::showCallUi() {
    CallWindow *w = ensureCallWindow();
    w->show();
    w->raise();
    w->activateWindow();
    m_chatHeader->setInCall(true);
}

void MainWindow::hideCallUi() {
    if (m_callWindow) m_callWindow->hide();
    m_chatHeader->setInCall(false);
}

// User-initiated calls reset the redial budget; the automatic path
// (attemptRedial) must not, or a flapping network would dial forever.
void MainWindow::onStartVideoCall() {
    m_redialAttempts = 0;
    startOutgoingCall(/*withVideo=*/true);
}
void MainWindow::onStartAudioCall() {
    m_redialAttempts = 0;
    startOutgoingCall(/*withVideo=*/false);
}

void MainWindow::startOutgoingCall(bool withVideo) {
    if (m_currentContact.id.isEmpty() || !m_currentContact.online) return;
    if (!m_webrtc) {
        // Demo mode (startDemo) never creates the media/signaling stack.
        statusBar()->showMessage(tr("Демо-режим — звонки недоступны"), 4000);
        return;
    }
    m_outgoingVideo = withVideo;
    m_activeCallPeer = m_currentContact.id;
    m_activeCallVideo = withVideo;
    m_callConnectedAt = QDateTime();
    CallWindow *w = ensureCallWindow();
    w->setPeerName(m_currentContact.displayName);
    w->showActive();
    w->setStatus(withVideo ? tr("Видеозвоним… ждём ответа")
                           : tr("Звоним… ждём ответа"));
    showCallUi();
    m_webrtc->prepare(withVideo);
    if (!m_webrtc->start()) {
        statusBar()->showMessage(tr("Не удалось запустить камеру/микрофон"), 5000);
        hideCallUi();
        return;
    }
    m_webrtc->startCall(m_currentContact.id);
}

void MainWindow::onIncomingOffer(const QString &fromPeerId, const QString &sdp,
                                 const QString &kind) {
    if (kind == QLatin1String("chat")) {
        // Silent text-delivery session — no ringing, no call window.
        ensureChatSession(fromPeerId)->acceptOffer(fromPeerId, sdp);
        return;
    }
    // Still ringing and the same caller offers again: their session was
    // rebuilt (ICE failed on their side mid-ring) — refresh the parked
    // offer, keep ringing.
    if (m_pendingOffer.peerId == fromPeerId) {
        m_pendingOffer.sdp   = sdp;
        m_pendingOffer.video = kind != QLatin1String("call-audio");
        return;
    }

    // Dropped-call recovery: answer silently — the user already said yes to
    // this conversation, don't make them find the green button again
    // mid-sentence. Two ways to get here:
    //  - resumesDroppedCall: our side noticed the failure first, hung up
    //    and armed the auto-accept window (onCallConnectionFailed);
    //  - resumesActiveCall: the CALLER noticed first and redials while our
    //    grace timer is still running. Our session is a zombie — replace
    //    it instead of answering "busy". Must be checked BEFORE the busy
    //    guard below, which would otherwise bounce the recovery attempt.
    const bool resumesDroppedCall = fromPeerId == m_autoAcceptPeer &&
        QDateTime::currentDateTime() < m_autoAcceptUntil;
    const bool resumesActiveCall = m_webrtc && m_webrtc->isInCall() &&
        m_webrtc->remotePeerId() == fromPeerId;
    if (resumesDroppedCall || resumesActiveCall) {
        if (resumesActiveCall) m_webrtc->hangup();
        m_autoAcceptPeer.clear();
        const int row = m_contactsModel->indexOf(fromPeerId);
        m_pendingOffer = {fromPeerId, sdp, kind != QLatin1String("call-audio")};
        CallWindow *w = ensureCallWindow();
        w->setPeerName(row >= 0 ? m_contactsModel->contactAt(row).displayName
                                : fromPeerId);
        w->showActive();
        w->setStatus(tr("Восстанавливаем связь…"));
        showCallUi();
        onAcceptIncomingCall();
        return;
    }

    // Busy: an active call (or an accept screen already on display) must
    // not be disturbed by a second caller — answer them "busy" and leave a
    // missed-call note instead.
    if ((m_webrtc && m_webrtc->isInCall()) || !m_pendingOffer.peerId.isEmpty()) {
        m_signaling->sendBye(fromPeerId);
        const int row = m_contactsModel->indexOf(fromPeerId);
        const QString name =
            row >= 0 ? m_contactsModel->contactAt(row).displayName : fromPeerId;
        logCallEvent(fromPeerId, tr("Звонил(а) вам, пока вы разговаривали"),
                     /*bad=*/true);
        statusBar()->showMessage(
            tr("%1 звонил(а), пока вы разговаривали").arg(name), 8000);
        return;
    }

    // A call: do NOT start the camera/microphone yet. Park the offer and
    // let the user decide with the green "Принять" button. The conversation
    // on the main window deliberately does NOT switch to the caller: if the
    // call goes unanswered, the user stays in whatever dialog they were
    // reading and the missed-call pill shows up as a red badge on the
    // caller's contact instead (logCallEvent bumps unread for non-current
    // peers). Switching happens on accept.
    const int callerRow = m_contactsModel->indexOf(fromPeerId);
    const QString callerName = callerRow >= 0
        ? m_contactsModel->contactAt(callerRow).displayName : fromPeerId;
    m_pendingOffer = {fromPeerId, sdp, kind != QLatin1String("call-audio")};
    ensureCallWindow()->showIncoming(callerName, m_pendingOffer.video);
    m_chatHeader->setInCall(true);   // header hang-up doubles as decline
    m_ringtone->start();
}

void MainWindow::onAcceptIncomingCall() {
    if (m_pendingOffer.peerId.isEmpty() || !m_webrtc) return;
    m_ringtone->stop();
    const PendingOffer offer = m_pendingOffer;
    m_pendingOffer = {};
    // The main window deliberately stays on whatever conversation was open
    // before the call — the call lives in its own window, and after hangup
    // the user continues reading where they left off.
    m_activeCallPeer = offer.peerId;
    m_activeCallVideo = offer.video;
    m_callConnectedAt = QDateTime();
    CallWindow *w = ensureCallWindow();
    w->showActive();
    w->setStatus(tr("Соединяем…"));
    m_chatHeader->setInCall(true);
    m_webrtc->prepare(offer.video);
    if (!m_webrtc->start()) {
        statusBar()->showMessage(tr("Не удалось запустить камеру/микрофон"), 5000);
        hideCallUi();
        return;
    }
    // A video conversation deserves the whole screen.
    if (offer.video) w->showMaximized();
    m_webrtc->acceptOffer(offer.peerId, offer.sdp);
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
    // The peer ended the conversation deliberately — stop any recovery.
    if (fromPeerId == m_autoAcceptPeer) m_autoAcceptPeer.clear();
    // Bye from someone who is NOT the active/pending call peer is just a
    // busy-decline of OUR outgoing offer or noise — never tear down the
    // current conversation because of it.
    if (m_pendingOffer.peerId == fromPeerId) {   // caller gave up ringing us
        m_ringtone->stop();
        logCallEvent(fromPeerId,
                     m_pendingOffer.video ? tr("Пропущенный видеозвонок")
                                          : tr("Пропущенный звонок"), /*bad=*/true);
        m_pendingOffer = {};
        hideCallUi();
        return;
    }
    if (!m_webrtc || m_webrtc->remotePeerId() != fromPeerId) return;
    const bool wasRinging = !m_webrtc->isChannelOpen();
    if (fromPeerId == m_activeCallPeer) {
        const QString what = m_activeCallVideo ? tr("Видеозвонок") : tr("Звонок");
        if (m_callConnectedAt.isValid()) {
            const qint64 secs = m_callConnectedAt.secsTo(QDateTime::currentDateTime());
            logCallEvent(m_activeCallPeer,
                         tr("%1 · %2 мин %3 сек").arg(what).arg(secs / 60).arg(secs % 60),
                         /*bad=*/false);
        } else {
            logCallEvent(m_activeCallPeer, tr("%1 отклонён").arg(what), /*bad=*/true);
        }
        m_activeCallPeer.clear();
    }
    m_webrtc->hangup();
    hideCallUi();
    if (wasRinging)
        statusBar()->showMessage(tr("Занято или звонок отклонён"), 6000);
}

void MainWindow::onLocalOffer(const QString &peerId, const QString &sdp) {
    // kind tells the callee whether to ask for the camera ("call") or run
    // voice-only ("call-audio") — and what to show on the accept screen.
    m_signaling->sendOffer(peerId, sdp,
                           m_outgoingVideo ? QStringLiteral("call")
                                           : QStringLiteral("call-audio"));
}
void MainWindow::onLocalAnswer(const QString &peerId, const QString &sdp) {
    m_signaling->sendAnswer(peerId, sdp);
}
void MainWindow::onLocalIce(const QString &peerId, const QString &candidate,
                             const QString &sdpMid, int sdpMLineIndex) {
    m_signaling->sendIce(peerId, candidate, sdpMid, sdpMLineIndex);
}

void MainWindow::onCallConnected() {
    if (m_callWindow) {
        m_callWindow->setStatus(tr("В разговоре"));
        // Caller side mirror of the accept-side maximize.
        if (m_activeCallVideo && !m_callWindow->isMaximized())
            m_callWindow->showMaximized();
    }
    if (!m_callConnectedAt.isValid()) m_callConnectedAt = QDateTime::currentDateTime();
    m_redialAttempts = 0;   // a fresh outage gets a fresh redial budget
}

void MainWindow::onCallEnded() {
    hideCallUi();
}

void MainWindow::onWebRtcError(const QString &message) {
    statusBar()->showMessage(tr("Медиа: %1").arg(message), 8000);
    // A pipeline ERROR is fatal for the session — clean up instead of
    // leaving a zombie call that blocks every following attempt.
    if (m_callWindow && m_callWindow->isVisible()) {
        m_callWindow->setStatus(tr("Ошибка: %1").arg(message));
        const QString peer = m_webrtc->remotePeerId();
        if (!peer.isEmpty() && m_signaling) m_signaling->sendBye(peer);
        m_webrtc->hangup();
    }
}

// --- Dropped-call recovery -------------------------------------------------
// ICE went "disconnected": usually a transient wifi/router blip that libnice
// heals by itself. Show what's happening instead of a frozen last frame.
void MainWindow::onCallConnectionInterrupted() {
    if (m_callWindow && m_callWindow->isVisible())
        m_callWindow->setStatus(tr("Связь прервалась, восстанавливаем…"));
}

void MainWindow::onCallConnectionRestored() {
    if (m_callWindow && m_callWindow->isVisible())
        m_callWindow->setStatus(tr("В разговоре"));
}

// ICE gave up (or the interruption outlived the grace timer). The session is
// dead — tear it down, then the original caller redials automatically and
// the callee silently accepts that redial (see onIncomingOffer), so neither
// side has to do anything to get the conversation back.
void MainWindow::onCallConnectionFailed() {
    if (m_activeCallPeer.isEmpty()) {
        m_webrtc->hangup();
        return;
    }
    const QString peer  = m_activeCallPeer;
    const bool    video = m_activeCallVideo;
    const bool    caller = m_webrtc->isCaller();
    logCallEvent(peer, tr("Связь оборвалась"), /*bad=*/true);
    m_activeCallPeer.clear();
    m_webrtc->hangup();   // hides the call window via callEnded

    if (caller && m_redialAttempts < 3) {
        ++m_redialAttempts;
        statusBar()->showMessage(tr("Связь оборвалась — перезваниваем…"), 8000);
        // Give the network a moment; attemptRedial keeps retrying while the
        // peer's presence is still catching up after the outage.
        QTimer::singleShot(2000, this, [this, peer, video] {
            attemptRedial(peer, video, /*triesLeft=*/5);
        });
    } else if (!caller) {
        // The caller is about to redial us — answer it without ringing.
        m_autoAcceptPeer  = peer;
        m_autoAcceptUntil = QDateTime::currentDateTime().addSecs(60);
        statusBar()->showMessage(
            tr("Связь оборвалась — ждём восстановления…"), 8000);
    }
}

void MainWindow::attemptRedial(const QString &peerId, bool video, int triesLeft) {
    // Somebody is already talking (or ringing) — don't barge in.
    if (m_webrtc->isInCall() || !m_pendingOffer.peerId.isEmpty()) return;
    const int row = m_contactsModel->indexOf(peerId);
    const bool online = row >= 0 && m_contactsModel->contactAt(row).online;
    if (!online) {
        // Presence drops out together with the network; poll a few times
        // while it comes back before giving up.
        if (triesLeft > 0)
            QTimer::singleShot(3000, this, [this, peerId, video, triesLeft] {
                attemptRedial(peerId, video, triesLeft - 1);
            });
        return;
    }
    selectContactById(peerId);
    startOutgoingCall(video);
}

void MainWindow::onHangupRequested() {
    // A deliberate hangup ends the conversation for real — no silent
    // auto-accept of a later call, no pending redials.
    m_autoAcceptPeer.clear();
    m_redialAttempts = 3;
    if (!m_pendingOffer.peerId.isEmpty()) {
        // Declining an incoming call we never accepted — media never started.
        m_ringtone->stop();
        m_signaling->sendBye(m_pendingOffer.peerId);
        logCallEvent(m_pendingOffer.peerId,
                     m_pendingOffer.video ? tr("Видеозвонок отклонён")
                                          : tr("Звонок отклонён"), /*bad=*/true);
        m_pendingOffer = {};
        hideCallUi();
        return;
    }
    // Tell the CALL peer we hung up — not whoever's conversation happens to
    // be open in the main window (they can differ now that incoming calls
    // no longer switch the dialog).
    const QString byePeer = !m_activeCallPeer.isEmpty()
        ? m_activeCallPeer
        : (m_webrtc ? m_webrtc->remotePeerId() : QString());
    if (!m_activeCallPeer.isEmpty()) {
        const QString what = m_activeCallVideo ? tr("Видеозвонок") : tr("Звонок");
        if (m_callConnectedAt.isValid()) {
            const qint64 secs = m_callConnectedAt.secsTo(QDateTime::currentDateTime());
            logCallEvent(m_activeCallPeer,
                         tr("%1 · %2 мин %3 сек").arg(what).arg(secs / 60).arg(secs % 60),
                         /*bad=*/false);
        } else {
            logCallEvent(m_activeCallPeer, tr("%1 отменён").arg(what), /*bad=*/true);
        }
        m_activeCallPeer.clear();
    }
    if (!byePeer.isEmpty()) m_signaling->sendBye(byePeer);
    m_webrtc->hangup();
    hideCallUi();
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
