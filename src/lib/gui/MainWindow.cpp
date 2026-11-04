/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2024 - 2026 Chris Rizzitello <sithord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2012 - 2024 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2008 Volker Lanz <vl@fidra.de>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit modifications: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
 */

#include "MainWindow.h"
#include "ui_MainWindow.h"

#include "Diagnostic.h"
#include "StyleUtils.h"

#include "dialogs/AboutDialog.h"
#include "dialogs/ClientConfigDialog.h"
#include "dialogs/FingerprintDialog.h"
#include "dialogs/HelpDialog.h"
#include "dialogs/ServerConfigDialog.h"
#include "dialogs/SettingsDialog.h"

#include "common/PlatformInfo.h"
#include "common/Settings.h"
#include "common/UrlConstants.h"
#include "common/VersionInfo.h"
#include "gui/Messages.h"
#include "gui/TlsUtility.h"
#include "gui/core/CoreProcess.h"
#include "gui/ipc/DaemonIpcClient.h"
#include "gui/widgets/LogDock.h"
#include "gui/OrbitDiscovery.h"
#include "gui/OrbitPanel.h"
#include "gui/OrbitRemoteControl.h"
#include "gui/widgets/ModeCard.h"
#include "gui/widgets/OrbitIcons.h"
#include "net/FingerprintDatabase.h"
#include "widgets/StatusBar.h"

#include <QCheckBox>
#include <QComboBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QFileDialog>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkInterface>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScreen>
#include <QScrollBar>
#include <QTimer>

#include <algorithm>
#include <memory>

#if defined(Q_OS_MACOS)
#include "gui/OSXScreenUnlockPassword.h"

#include <ApplicationServices/ApplicationServices.h>
#endif

using namespace deskflow::gui;

MainWindow::MainWindow()
    : ui{std::make_unique<Ui::MainWindow>()},
      m_coreProcess(m_serverConfig),
      m_trayIcon{new QSystemTrayIcon(this)},
      m_guiDupeChecker{new QLocalServer(this)},
      m_daemonIpcClient{new ipc::DaemonIpcClient(this)},
      m_logDock{new LogDock(this)},
      m_statusBar{new StatusBar(this)},
      m_menuFile{new QMenu(this)},
      m_menuEdit{new QMenu(this)},
      m_menuView{new QMenu(this)},
      m_menuHelp{new QMenu(this)},
      m_actionAbout{new QAction(this)},
      m_actionMinimize{new QAction(this)},
      m_actionQuit{new QAction(this)},
      m_actionTrayQuit{new QAction(this)},
      m_actionRestore{new QAction(this)},
      m_actionSettings{new QAction(this)},
      m_actionStartCore{new QAction(this)},
      m_actionRestartCore{new QAction(this)},
      m_actionStopCore{new QAction(this)},
      m_actionShowHelp{new QAction(this)},
      m_networkMonitor{new NetworkMonitor(this)}
{
  ui->setupUi(this);

  setWindowIcon(QIcon::fromTheme(kRevFqdnName));

  addDockWidget(Qt::BottomDockWidgetArea, m_logDock);

  // Setup Actions
  m_actionAbout->setMenuRole(QAction::AboutRole);
  m_actionAbout->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::HelpAbout));

  m_actionMinimize->setIcon(QIcon::fromTheme(QStringLiteral("window-minimize-pip")));
  m_actionRestore->setIcon(QIcon::fromTheme(QStringLiteral("window-restore-pip")));

  if (!deskflow::platform::isWindows()) {
    m_actionQuit->setShortcut(QKeySequence::Quit);
    m_actionTrayQuit->setShortcut(QKeySequence::Quit);
  }

  m_actionQuit->setIcon(QIcon::fromTheme("application-exit"));
  m_actionQuit->setMenuRole(QAction::QuitRole);

  m_actionTrayQuit->setIcon(QIcon::fromTheme("application-exit"));
  m_actionTrayQuit->setMenuRole(QAction::NoRole);

  m_actionSettings->setIcon(QIcon::fromTheme(QStringLiteral("configure")));
  m_actionSettings->setMenuRole(QAction::PreferencesRole);
  m_actionSettings->setShortcut(QKeySequence::Preferences);

  m_actionStartCore->setIcon(QIcon::fromTheme(QStringLiteral("system-run")));
  m_actionStartCore->setMenuRole(QAction::NoRole);

  m_actionRestartCore->setVisible(false);
  m_actionRestartCore->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh")));
  m_actionRestartCore->setMenuRole(QAction::NoRole);

  m_actionStopCore->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::ProcessStop));
  m_actionStopCore->setMenuRole(QAction::NoRole);

  m_actionShowHelp->setIcon(QIcon::fromTheme(QStringLiteral("question")));
  m_actionShowHelp->setMenuRole(QAction::NoRole);
  m_actionShowHelp->setShortcut(QKeySequence::HelpContents);

  // Setup the Instance Checking
  // In case of a previous crash remove first
  QLocalServer::removeServer(m_guiSocketName);
  m_guiDupeChecker->listen(m_guiSocketName);

  createMenuBar();
  setupControls();
  updateText();
  connectSlots();
  setupTrayIcon();
  updateComputerName();
  setHelpFilePath();

  qDebug().noquote() << "active settings path:" << Settings::settingsPath();

  // Force generation of a certificate on the host
  if (TlsUtility::isEnabled()) {
    if (Settings::value(Settings::Security::KeySize).toInt() < 2048) {
      QMessageBox::information(
          this, kAppName,
          tr("Your current TLS key is smaller than the minimum allowed size, A new key 2048-bit key will be generated.")
      );
      Settings::setValue(Settings::Security::KeySize, 2048);
    }
    if (!TlsUtility::isCertValid()) {
      generateCertificate();
    } else {
      m_fingerprint = {QCryptographicHash::Sha256, TlsUtility::certFingerprint()};
    }
  }

  applyConfig();
  m_statusBar->setSecurityIcon(TlsUtility::isEnabled());
  restoreWindow();

#ifdef Q_OS_MACOS
  // Native quits must bypass close-to-tray, just like the panel's Quit.
  // macOSNativeHide() hides the app without changing Qt's isVisible(), so
  // visibility cannot distinguish a quit request from closing a window.
  installQuitHandler([this] {
    m_quitting = true;
    return true;
  });
#endif
}
MainWindow::~MainWindow()
{
  // Stop network monitoring
  if (m_networkMonitor) {
    m_networkMonitor->stopMonitoring();
  }

  m_guiDupeChecker->close();
  m_coreProcess.cleanup();
}

void MainWindow::restoreWindow()
{
  auto windowGeometry = Settings::value(Settings::Gui::WindowGeometry).toRect();
  const auto totalGeometry = QGuiApplication::primaryScreen()->availableGeometry();
  adjustSize();
  if (windowGeometry.isValid()) {
    move(windowGeometry.topLeft());
  }
  windowGeometry = geometry();
  m_expandedSize = geometry().size();

  if (!totalGeometry.contains(windowGeometry)) {
    QRect screenGeometry = QGuiApplication::primaryScreen()->geometry();
    move(screenGeometry.center() - rect().center());
  }

  setFixedSize(size());
}

void MainWindow::setupControls()
{
  secureSocket(false);

  updateConfigureButtons();

  // no computer name / IP row, no export button
  ui->widget->hide();
  ui->btnSaveServerConfig->hide();

  if (Settings::value(Settings::Core::LastVersion).toString() != kVersion) {
    Settings::setValue(Settings::Core::LastVersion, kVersion);
  }

  // the log panel is hidden for now (the log is still written to file)
  m_logDock->hide();

  ui->serverOptions->setVisible(false);
  ui->clientOptions->setVisible(false);

  const auto coreMode = Settings::value(Settings::Core::CoreMode).value<Settings::CoreMode>();
  ui->rbModeClient->setChecked(coreMode == Settings::CoreMode::Client);
  ui->rbModeServer->setChecked(coreMode == Settings::CoreMode::Server);

  // two large cards choose the mode; the radio buttons stay as the hidden model
  m_serverCard = new ModeCard(ModeCard::Role::Server, ui->rbModeServer->text(), this);
  m_clientCard = new ModeCard(ModeCard::Role::Client, ui->rbModeClient->text(), this);
  ui->rbModeServer->hide();
  ui->rbModeClient->hide();
  ui->horizontalLayout->setSpacing(12);
  ui->horizontalLayout->addWidget(m_serverCard);
  ui->horizontalLayout->addWidget(m_clientCard);
  connect(m_serverCard, &QAbstractButton::clicked, this, [this] { ui->rbModeServer->setChecked(true); });
  connect(m_clientCard, &QAbstractButton::clicked, this, [this] { ui->rbModeClient->setChecked(true); });
  const auto syncCards = [this] {
    m_serverCard->setSelected(ui->rbModeServer->isChecked());
    m_clientCard->setSelected(ui->rbModeClient->isChecked());
  };
  connect(ui->rbModeServer, &QRadioButton::toggled, this, syncCards);
  connect(ui->rbModeClient, &QRadioButton::toggled, this, syncCards);
  syncCards();

  m_panel = new OrbitPanel(this);
  connect(m_panel, &OrbitPanel::actionRequested, this, &MainWindow::handlePanelAction);
#ifdef Q_OS_MACOS
  // the panel's own menu bar icon (Core Animation) replaces the Qt tray icon
  m_panel->installStatusItem();
  // the server unlocks a locked client after Touch ID in that icon
  connect(&m_coreProcess, &CoreProcess::touchIdRequested, m_panel, &OrbitPanel::beginTouchId);
  connect(&m_coreProcess, &CoreProcess::touchIdCancelled, m_panel, &OrbitPanel::cancelTouchId);
  connect(m_panel, &OrbitPanel::touchIdFinished, &m_coreProcess, &CoreProcess::sendTouchIdResult);
  connect(m_panel, &OrbitPanel::touchIdNotShown, &m_coreProcess, &CoreProcess::sendTouchIdNotShown);
#endif
  connect(m_statusBar, &StatusBar::statusChanged, this, &MainWindow::updatePanel);

  // the client picks a server found on the network; the address field stays
  // as the model and appears only for "Enter address..."
  m_advertiser = new orbit::discovery::Advertiser(this);
  m_browser = new orbit::discovery::Browser(this);
  m_serverPicker = new QComboBox(this);
  m_serverPicker->setMinimumWidth(220);
  m_serverPicker->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  ui->horizontalLayout_3->insertWidget(ui->horizontalLayout_3->indexOf(ui->lineHostname), m_serverPicker);
  ui->lineHostname->setVisible(false);
  ui->lineHostname->setPlaceholderText(tr("Name or IP address"));
  connect(m_browser, &orbit::discovery::Browser::serversChanged, this, &MainWindow::updateServerPicker);
  m_remoteListener = new orbit::remote::Listener(this);
  m_remoteSender = new orbit::remote::Sender(this);
  connect(
      m_remoteListener, &orbit::remote::Listener::becomeClientRequested, this, &MainWindow::handleBecomeClientRequest
  );
  connect(m_remoteSender, &orbit::remote::Sender::finished, this, &MainWindow::setAsClientFinished);
  m_clientConnectingTimer = new QTimer(this);
  m_clientConnectingTimer->setSingleShot(true);
  connect(m_clientConnectingTimer, &QTimer::timeout, this, &MainWindow::clientConnectingEnded);
  // a client tries again every few seconds while it can't connect
  connect(&m_coreProcess, &CoreProcess::clientConnecting, this, [this] { showClientConnecting(5000); });
  connect(m_serverPicker, &QComboBox::activated, this, &MainWindow::serverPicked);
  connect(ui->lineHostname, &QLineEdit::textChanged, this, &MainWindow::updateServerPicker);
  updateServerPicker();

  ui->lineEditName->setValidator(new QRegularExpressionValidator(m_nameRegEx, this));
  ui->lineEditName->setVisible(false);
  ui->lineEditName->installEventFilter(this);

  if (deskflow::platform::isMac()) {
    ui->rbModeServer->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->rbModeClient->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->btnSaveServerConfig->setFixedWidth(ui->btnSaveServerConfig->height());
  } else {
    ui->btnSaveServerConfig->setIconSize(QSize(22, 22));
  }
  setStatusBar(m_statusBar);
}

//////////////////////////////////////////////////////////////////////////////
// Begin slots
//////////////////////////////////////////////////////////////////////////////

// remember: using queued connection allows the render loop to run before
// executing the slot. the default is to instantly call the slot when the
// signal is emitted from the thread that owns the receiver's object.
void MainWindow::connectSlots()
{
  connect(Settings::instance(), &Settings::serverSettingsChanged, this, &MainWindow::serverConfigSaving);
  connect(Settings::instance(), &Settings::settingsChanged, this, &MainWindow::settingsChanged);

  connect(&m_coreProcess, &CoreProcess::error, this, &MainWindow::coreProcessError);
  connect(&m_coreProcess, &CoreProcess::logLine, this, &MainWindow::handleLogLine);
  connect(
      &m_coreProcess, &CoreProcess::processStateChanged, this, &MainWindow::coreProcessStateChanged,
      Qt::QueuedConnection
  );
  connect(&m_coreProcess, &CoreProcess::connectionStateChanged, this, &MainWindow::coreConnectionStateChanged);
  connect(&m_coreProcess, &CoreProcess::secureSocket, this, &MainWindow::secureSocket);
  connect(
      &m_coreProcess, &CoreProcess::daemonIpcClientConnectionFailed, this, &MainWindow::daemonIpcClientConnectionFailed
  );
  connect(&m_coreProcess, &CoreProcess::securityLevelChanged, m_statusBar, &StatusBar::setSecurityLevel);

  connect(m_actionAbout, &QAction::triggered, this, &MainWindow::openAboutDialog);
  connect(m_actionMinimize, &QAction::triggered, this, &MainWindow::hide);

  connect(m_actionQuit, &QAction::triggered, this, &MainWindow::quit);
  connect(m_actionTrayQuit, &QAction::triggered, this, &MainWindow::quit);
  connect(m_actionRestore, &QAction::triggered, this, &MainWindow::showAndActivate);
  connect(m_actionSettings, &QAction::triggered, this, &MainWindow::openSettings);
  connect(m_actionStartCore, &QAction::triggered, this, &MainWindow::startCore);
  connect(m_actionRestartCore, &QAction::triggered, this, &MainWindow::resetCore);
  connect(m_actionStopCore, &QAction::triggered, this, &MainWindow::stopCore);
  connect(m_actionShowHelp, &QAction::triggered, this, &MainWindow::showHelpViewer);

  // clicking the menu bar icon opens the panel
  connect(m_trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::trayIconActivated);

  connect(&m_coreProcess, &CoreProcess::connectedClientsChanged, this, &MainWindow::serverClientsChanged);
  connect(&m_coreProcess, &CoreProcess::unrecognisedClient, this, &MainWindow::handleUnrecognisedClient);
  connect(&m_coreProcess, &CoreProcess::connectionRefused, this, &MainWindow::handleConnectionRefused);
  connect(&m_coreProcess, &CoreProcess::retryIn, this, &MainWindow::updateTimeoutDelay);
  connect(&m_coreProcess, &CoreProcess::peerFingerprint, this, &MainWindow::handlePeerFingerprint);
  connect(&m_coreProcess, &CoreProcess::missingKeyboardLayouts, this, &MainWindow::handleMissingKeyboardLayouts);

  if (Settings::value(Settings::Gui::AutoStartCore).toBool()) {
    connect(ui->btnToggleCore, &QPushButton::clicked, m_actionStopCore, &QAction::trigger, Qt::UniqueConnection);
  } else {
    connect(ui->btnToggleCore, &QPushButton::clicked, m_actionStartCore, &QAction::trigger, Qt::UniqueConnection);
  }

  connect(ui->btnRestartCore, &QPushButton::clicked, this, &MainWindow::resetCore);

  connect(ui->lineHostname, &QLineEdit::returnPressed, this, [this] {
    if (m_coreProcess.processState() == ProcessState::Stopped)
      startCore();
    else
      m_coreProcess.restart();
  });
  connect(ui->lineHostname, &QLineEdit::textChanged, this, &MainWindow::remoteHostChanged);

  connect(ui->btnSaveServerConfig, &QPushButton::clicked, this, &MainWindow::saveServerConfig);
  connect(ui->btnConfigureServer, &QPushButton::clicked, this, [this] { showConfigureServer(""); });
  connect(ui->btnConfigureClient, &QPushButton::clicked, this, [this] { showConfigureClient(); });
  connect(ui->lblComputerName, &QLabel::linkActivated, this, &MainWindow::openSettings);

  connect(ui->rbModeServer, &QRadioButton::toggled, this, &MainWindow::coreModeToggled);
  connect(ui->rbModeClient, &QRadioButton::toggled, this, &MainWindow::coreModeToggled);


  connect(m_statusBar, &StatusBar::requestShowMyFingerprints, this, &MainWindow::showMyFingerprint);
  connect(m_statusBar, &StatusBar::requestUpdateVersion, this, &MainWindow::openGetNewVersionUrl);
  connect(&m_versionChecker, &VersionChecker::updateFound, m_statusBar, &StatusBar::updateFound);

  connect(m_guiDupeChecker, &QLocalServer::newConnection, this, &MainWindow::showAndActivate);

  connect(ui->btnEditName, &QPushButton::clicked, this, &MainWindow::showHostNameEditor);

  connect(ui->lineEditName, &QLineEdit::editingFinished, this, &MainWindow::setHostName);

  connect(m_networkMonitor, &NetworkMonitor::ipAddressesChanged, this, &MainWindow::updateIpLabel);
}

void MainWindow::toggleLogVisible(bool visible)
{
  // When the main window is hidden e.g. close to tray / minimized), this also triggers the log visibility toggle,
  // but we don't want to hide the log in this case since we would need to un-hide it when the window is shown again.
  if (!isVisible() || isMinimized()) {
    qDebug() << "not toggling log, window not visible";
    return;
  }

  setFixedSize(16777215, 16777215);
  Settings::setValue(Settings::Gui::LogExpanded, visible);
  if (visible) {
    if (m_logDock->isFloating()) {
      adjustSize();
      setFixedSize(size());
    } else {
      QTimer::singleShot(15, this, [&] { resize(m_expandedSize); });
    }
  } else {
    if (!m_logDock->isFloating()) {
      m_expandedSize = geometry().size();
    }
    m_logDock->hide();
    if (!m_logDock->isFloating()) {
      adjustSize();
    }
    setFixedSize(size());
  }
  Settings::setValue(Settings::Gui::WindowGeometry, geometry());
}

void MainWindow::settingsChanged(const QString &key)
{
  if (key == Settings::Log::Level) {
    m_coreProcess.applyLogLevel();
    return;
  }

  if (key == Settings::Core::ComputerName)
    updateComputerName();

  if ((key == Settings::Security::Certificate) || (key == Settings::Security::KeySize) ||
      (key == Settings::Security::TlsEnabled) || (key == Settings::Security::CheckPeers)) {
    if (TlsUtility::isEnabled()) {
      if (!TlsUtility::isCertValid()) {
        qWarning() << tr("invalid certificate, generating a new one");
        TlsUtility::generateCertificate();
      }
      m_fingerprint = {QCryptographicHash::Sha256, TlsUtility::certFingerprint()};
      updateFingerprintButton();
    }
    updateSecurityIcon(m_statusBar->securityIconVisible());
    return;
  }
}

void MainWindow::serverConfigSaving()
{
  m_serverConfig.commit();
}

void MainWindow::trayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
  // a plain click is a Trigger; clicks that do not come from the mouse
  // (accessibility, scripting) are Unknown
  if (reason == QSystemTrayIcon::Context || reason == QSystemTrayIcon::MiddleClick)
    return;
  qDebug() << "menu bar icon clicked, toggling the panel";
  m_panel->toggle();
}

void MainWindow::coreProcessError(CoreProcess::Error error)
{
  if (error == CoreProcess::Error::AddressMissing) {
    QMessageBox::warning(
        this, tr("Address missing"), tr("Please enter the hostname or IP address of the other computer.")
    );
  } else if (error == CoreProcess::Error::StartFailed) {
    show();
    auto message = tr("The Core executable could not be started.\n"
                      "Please check if you have sufficient permissions to run %1.")
                       .arg(kCoreBinName);

    if (Settings::value(Settings::Core::CoreMode) == Settings::CoreMode::Server) {
      const auto mode =
          Settings::value(Settings::Server::ExternalConfigFile).toBool() ? tr("read") : tr("read and write");
      message.append(tr("\nAdditionally, check you are able to %1 the server config file: %2")
                         .arg(mode, Settings::serverConfigFile()));
    }
    QMessageBox::warning(this, kAppName, message);
  }
}

void MainWindow::startCore()
{
  // Save current IP state when server starts
  if (m_coreProcess.mode() == CoreMode::Server && Settings::value(Settings::Core::Interface).toString().isEmpty()) {
    m_serverStartIPs = NetworkMonitor::validAddresses();
    m_serverStartSuggestedIP = m_serverStartIPs.isEmpty() ? "" : m_serverStartIPs.first();
  }

  m_actionStartCore->setVisible(false);
  m_actionRestartCore->setVisible(true);
  m_coreProcess.start();
}

void MainWindow::stopCore()
{
  qDebug() << "stopping core process";
  m_coreProcess.stop();
  m_actionStartCore->setVisible(true);
  m_actionRestartCore->setVisible(false);
}

void MainWindow::clearSettings()
{
  qDebug() << "clearing settings";

  m_networkMonitor->stopMonitoring();

  disconnect(&m_coreProcess, nullptr, this, nullptr);
  disconnect(&m_versionChecker, nullptr, this, nullptr);
  disconnect(m_guiDupeChecker, nullptr, this, nullptr);
  disconnect(m_trayIcon, nullptr, this, nullptr);
  disconnect(m_logDock->toggleViewAction(), nullptr, this, nullptr);

  m_coreProcess.stop();
  m_coreProcess.clearSettings();

  m_saveOnExit = false;
  diagnostic::clearSettings(true);
}

bool MainWindow::saveServerConfig()
{
  QString fileName = QFileDialog::getSaveFileName(this, tr("Save server configuration as..."));

  if (!fileName.isEmpty() && !m_serverConfig.save(fileName)) {
    QMessageBox::warning(this, tr("Save failed"), tr("Could not save server configuration to file."));
    return true;
  }

  return false;
}

void MainWindow::openAboutDialog()
{
  AboutDialog about(this);
  about.exec();
}

void MainWindow::openGetNewVersionUrl() const
{
  QDesktopServices::openUrl(QUrl(kUrlDownload));
}

void MainWindow::openSettings()
{
  auto dialog = SettingsDialog(this, m_serverConfig);

  connect(&dialog, &SettingsDialog::requestRemoveAllSettings, this, &MainWindow::clearSettings, Qt::UniqueConnection);
  if (dialog.exec() == QDialog::Accepted) {
    Settings::save();
    disconnect(&dialog, &SettingsDialog::requestRemoveAllSettings, nullptr, nullptr);

    applyConfig();

    if (m_coreProcess.isStarted()) {
      m_coreProcess.restart();
    }
  }
}

void MainWindow::resetCore()
{
  if (m_coreProcess.processState() == ProcessState::Stopped)
    startCore();
  else
    m_coreProcess.restart();
}

void MainWindow::showMyFingerprint()
{
  FingerprintDialog fingerprintDialog(this, m_fingerprint);
  fingerprintDialog.exec();
}

void MainWindow::coreModeToggled(bool checked)
{
  // this method is called when rbClient or rbServer toggles
  // with both being in the same group one must be turned on if the other is turned off
  // only react to toggle on to avoid calling everything twice when the user switches modes
  if (!checked)
    return;

  Settings::CoreMode mode = Settings::CoreMode::None;

  if (ui->rbModeServer->isChecked())
    mode = Settings::CoreMode::Server;
  if (ui->rbModeClient->isChecked())
    mode = Settings::CoreMode::Client;

  qDebug() << QStringLiteral("change mode to: %1").arg(QVariant::fromValue(mode).toString());

  if (m_coreProcess.isStarted() && m_coreProcess.mode() != mode)
    m_coreProcess.stop();
  m_coreProcess.setMode(mode);

  Settings::setValue(Settings::Core::CoreMode, mode);
  if (mode == Settings::CoreMode::Server && !Settings::value(Settings::Server::ExternalConfig).toBool() &&
      m_serverConfig.ensureServer()) {
    qInfo("placing this computer in the arrangement");
    m_serverConfig.commit();
  }
  Settings::save();

  updateModeControls();
  updatePanel();
}

void MainWindow::updateModeControls()
{
  const auto mode = m_coreProcess.mode();
  const bool isServer = mode == Settings::CoreMode::Server;
  const bool isClient = mode == Settings::CoreMode::Client;

  ui->serverOptions->setVisible(isServer);
  ui->clientOptions->setVisible(isClient);
  ui->lblNoMode->setVisible(!isServer && !isClient);
  toggleCanRunCore(canRunCore());

  ui->lblIpAddresses->setVisible(
      (isClient && !Settings::value(Settings::Core::Interface).toString().isEmpty()) || isServer
  );

  if (ui->lblIpAddresses->isVisible())
    updateNetworkInfo();

  if (isServer) {
    m_networkMonitor->startMonitoring();
  } else {
    m_networkMonitor->stopMonitoring();
  }

  // a client lists the servers to connect to; a server lists the other
  // servers, to set them as its clients
  if (isClient || isServer)
    m_browser->start();
  else
    m_browser->stop();
  updateAdvertising();

  updatePanel();

  // no start/stop button: the core starts on its own (see startCoreIfReady)
  ui->btnToggleCore->setVisible(false);
  if (isServer || isClient)
    startCoreIfReady();

  if (isServer || isClient)
    updateModeControlLabels();
}

void MainWindow::updateModeControlLabels()
{
  const bool isServer = m_coreProcess.mode() == CoreMode::Server;
  const bool isStarted = m_coreProcess.isStarted();

  QString startText;
  QString stopText;
  QIcon startIcon;
  QIcon stopIcon;

  if (isServer) {
    startText = tr("Start");
    stopText = tr("Stop");
    startIcon = QIcon::fromTheme(QStringLiteral("system-run"));
    stopIcon = QIcon::fromTheme(QIcon::ThemeIcon::ProcessStop);
  } else {
    startText = tr("Connect");
    stopText = tr("Disconnect");
    startIcon = QIcon::fromTheme(QStringLiteral("network-connect"));
    stopIcon = QIcon::fromTheme(QStringLiteral("network-disconnect"));
  }

  m_actionStartCore->setText(startText);
  m_actionStartCore->setIcon(startIcon);
  m_actionStopCore->setText(stopText);
  m_actionStopCore->setIcon(stopIcon);

  if (isStarted) {
    ui->btnToggleCore->setText(stopText);
    ui->btnToggleCore->setIcon(stopIcon);
  } else {
    ui->btnToggleCore->setText(startText);
    ui->btnToggleCore->setIcon(startIcon);
  }
}

void MainWindow::updateSecurityIcon(bool visible)
{
  m_statusBar->setSecurityIconVisible(visible);
  if (!visible)
    return;
  m_statusBar->setSecurityIcon(TlsUtility::isEnabled());
}

void MainWindow::updateNetworkInfo()
{
  const auto mode = m_coreProcess.mode();
  if (mode == CoreMode::None) {
    return;
  }

  if (mode == CoreMode::Server)
    updateIpLabel(NetworkMonitor::validAddresses());
  else
    updateIpLabel({Settings::value(Settings::Core::Interface).toString()});
}

void MainWindow::serverConnectionConfigureClient(const QString &clientName)
{
  m_serverConfigDialogVisible = true;
  ServerConfigDialog dialog(this, m_serverConfig);
  if (dialog.addClient(clientName) && dialog.exec() == QDialog::Accepted) {
    m_coreProcess.restart();
  }
  m_serverConfigDialogVisible = false;
}

//////////////////////////////////////////////////////////////////////////////
// End slots
//////////////////////////////////////////////////////////////////////////////

void MainWindow::open()
{
  // Orbit lives in the menu bar: its panel holds the whole interface, this
  // window is never shown (macOS hides it after this function ends)
  QTimer::singleShot(1, this, &MainWindow::hide);
  updatePanel();

  // if a critical error was shown just before the main window (i.e. on app
  // load), it will be hidden behind the main window. therefore we need to raise
  // it up in front of the main window.
  // HACK: because the `onShown` event happens just as the window is shown, the
  // message box has a chance of being raised under the main window. to solve
  // this we delay the error dialog raise by a split second. this seems a bit
  // hacky and fragile, so maybe there's a better approach.
  const auto kCriticalDialogDelay = 100;
  QTimer::singleShot(kCriticalDialogDelay, this, &messages::raiseCriticalDialog);

  // Orbit has no update server, so it never checks for new versions

  if (Settings::value(Settings::Gui::AutoStartCore).toBool()) {
    if (ui->rbModeClient->isChecked() && ui->lineHostname->text().isEmpty())
      return;
    startCore();
  }
}

void MainWindow::createMenuBar()
{
  m_menuFile->addAction(m_actionStartCore);
  m_menuFile->addAction(m_actionRestartCore);
  m_menuFile->addAction(m_actionStopCore);
  m_menuFile->addSeparator();
  m_menuFile->addAction(m_actionQuit);

  m_menuEdit->addAction(m_actionSettings);

  m_menuHelp->addAction(m_actionAbout);
  m_menuHelp->addAction(m_actionShowHelp);

  auto menuBar = new QMenuBar(this);
  menuBar->addMenu(m_menuFile);
  menuBar->addMenu(m_menuEdit);
  menuBar->addMenu(m_menuHelp);

  setMenuBar(menuBar);
}

void MainWindow::setupTrayIcon()
{
  // no menu: clicking the menu bar icon opens the panel (trayIconActivated).
  // On macOS the panel has its own menu bar icon (OrbitPanel::installStatusItem).
  if (deskflow::platform::isMac())
    return;
  setTrayIcon();
  m_trayIcon->show();
}

void MainWindow::applyConfig()
{
  if (Settings::value(Settings::Gui::ShowVersionInTitle).toBool()) {
    setWindowTitle(QStringLiteral("%1 - %2").arg(kAppName, kDisplayVersion));
  } else {
    setWindowTitle(kAppName);
  }

  if (const auto host = Settings::value(Settings::Client::RemoteHost).toString(); !host.isEmpty())
    ui->lineHostname->setText(host);

  updateFingerprintButton();
  setTrayIcon();

  if (const auto ip = Settings::value(Settings::Core::Interface).toString(); !ip.isEmpty()) {
    m_serverStartIPs = {ip};
    m_serverStartSuggestedIP = ip;
  }

  coreModeToggled(true);
}

void MainWindow::saveSettings() const
{
  if (ui->rbModeClient->isChecked()) {
    Settings::setValue(Settings::Core::CoreMode, Settings::CoreMode::Client);
  } else if (ui->rbModeServer->isChecked()) {
    Settings::setValue(Settings::Core::CoreMode, Settings::CoreMode::Server);
  }
  if (!ui->lineHostname->text().isEmpty())
    Settings::setValue(Settings::Client::RemoteHost, ui->lineHostname->text());
  Settings::save();
}

void MainWindow::setTrayIcon()
{
  static const auto fallbackPath = QStringLiteral(":/icons/%1-%2/apps/64/%3");

  QString themeIcon = kRevFqdnName;
  // macOS: the panel draws its own menu bar icon
  if (deskflow::platform::isMac())
    return;
  if (!Settings::value(Settings::Gui::SymbolicTrayIcon).toBool()) {
    if (deskflow::platform::isMac())
      m_trayIcon->setIcon(QIcon::fromTheme(themeIcon));
    else
      m_trayIcon->setIcon(QIcon(fallbackPath.arg(kAppId, QStringLiteral("dark"), themeIcon)));
    return;
  }

  themeIcon.append(QStringLiteral("-symbolic"));

  if (deskflow::platform::isWindows()) {
    QSettings settings(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
        QSettings::NativeFormat
    );
    const QString theme = settings.value(QStringLiteral("SystemUsesLightTheme"), 1).toBool() ? QStringLiteral("light")
                                                                                             : QStringLiteral("dark");
    m_trayIcon->setIcon(QIcon(fallbackPath.arg(kAppId, theme, themeIcon)));
    return;
  }

  auto icon = QIcon::fromTheme(themeIcon, QIcon(fallbackPath.arg(kAppId, iconMode(), themeIcon)));
  icon.setIsMask(true);
  m_trayIcon->setIcon(icon);
}

void MainWindow::handleLogLine(const QString &line)
{
  m_logDock->appendLine(line);
}

void MainWindow::handleUnrecognisedClient(const QString &clientName)
{
  if (m_ignoredClients.contains(clientName)) {
    qDebug("ignoring %s:", qPrintable(clientName));
    return;
  }

  if (m_newClientPromptShowing || m_serverConfigDialogVisible)
    return;

  if (Settings::value(Settings::Server::ExternalConfig).toBool())
    return;

  if (m_serverConfig.isFull() || m_serverConfig.computerExists(clientName))
    return;

  // a new computer appears in the arrangement as soon as it connects: add it
  // next to the server, then reload the server's config so it is accepted
  // when it tries again (the other clients stay connected)
  qInfo().noquote() << "adding new client" << clientName << "next to the server";
  m_serverConfig.addClient(clientName);
  Settings::setValue(Settings::Computer::Name.arg(clientName), clientName);
  m_serverConfig.commit();
  Settings::save();
  updatePanel();
  m_coreProcess.reloadConfig();
}

void MainWindow::handleConnectionRefused(deskflow::core::ConnectionRefusal reason)
{
  if (reason != deskflow::core::ConnectionRefusal::AlreadyConnected)
    return;

  if (!isVisible() || m_clientErrorVisible)
    return;

  m_clientErrorVisible = true;
  showAndActivate();

  const auto address = Settings::value(Settings::Client::RemoteHost).toString();
  QMessageBox::warning(
      this, tr("%1 Connection Error").arg(kAppName),
      tr("<p>Failed to connect to the server '%1'.</p>"
         "<p>A Client with your name is already connected to the server.</p>"
         "Please ensure that you're using a unique name and that only a "
         "single instance of the client process is running.</p>")
          .arg(address)
  );

  m_clientErrorVisible = false;
}

void MainWindow::handleMissingKeyboardLayouts(const QString &layouts)
{
  if (Settings::value(Settings::Gui::IgnoreMissingKeyboardLayouts).toBool())
    return;

  QMessageBox msgBox(this);
  msgBox.setIcon(QMessageBox::Warning);
  msgBox.setWindowTitle(tr("Missing Keyboard Layouts"));
  msgBox.setText(tr("<p>Keyboard layout support requires matching layouts on all computers. "
                    "The following layouts from the other computer are not installed on this computer:</p>"
                    "<p><b>%1</b></p>"
                    "<p>Please install them to enable support for these layouts.</p>")
                     .arg(layouts));

  auto *checkBox = new QCheckBox(tr("Don't show this again"), &msgBox);
  msgBox.setCheckBox(checkBox);
  msgBox.exec();

  if (checkBox->isChecked()) {
    Settings::setValue(Settings::Gui::IgnoreMissingKeyboardLayouts, true);
  }
}

void MainWindow::handlePeerFingerprint(const QString &fingerprint)
{
  // "<fingerprint> <computer name>"; peers with an older certificate send
  // no name
  const auto separator = fingerprint.indexOf(' ');
  const auto sha256Text = QString(fingerprint.left(separator)).remove(':');
  const auto peerName = separator < 0 ? QString() : fingerprint.mid(separator + 1).trimmed();
  const Fingerprint sha256 = {QCryptographicHash::Sha256, QByteArray::fromHex(sha256Text.toLatin1())};

  const bool isClient = m_coreProcess.mode() == CoreMode::Client;
  if ((isClient && m_checkedServers.contains(sha256Text)) || (!isClient && m_checkedClients.contains(sha256Text))) {
    qDebug("ignoring fingerprint, already handled");
    return;
  }

  FingerprintDatabase db;
  db.read(trustedFingerprintDatabase());

  if (db.isTrusted(sha256)) {
    qDebug("fingerprint is trusted");
    return;
  }

  if (isClient) {
    m_checkedServers.append(sha256Text);
    m_coreProcess.stop();
  } else {
    m_checkedClients.append(sha256Text);
  }

  if (!askToTrustPeer(isClient, peerName)) {
    qInfo().noquote() << (isClient ? "connection to server refused:" : "client refused:")
                      << (peerName.isEmpty() ? sha256Text : peerName);
    return;
  }

  db.addTrusted(sha256);
  if (!db.write(trustedFingerprintDatabase())) {
    qCritical().noquote() << "unable to write fingerprint to db:" << trustedFingerprintDatabase();
    m_coreProcess.stop();
    return;
  }
  if (isClient) {
    m_checkedServers.removeAll(sha256Text);
    m_coreProcess.start();
  } else {
    m_checkedClients.removeAll(sha256Text);
  }
}

bool MainWindow::askToTrustPeer(bool isClient, const QString &peerName)
{
  // a server is known by the name it was picked by when its certificate
  // carries none
  auto name = peerName;
  if (name.isEmpty() && isClient && m_serverPicker != nullptr)
    name = m_serverPicker->currentText();
  if (name.isEmpty() && isClient)
    name = Settings::value(Settings::Client::RemoteHost).toString();

  QMessageBox box(this);
  box.setIcon(QMessageBox::Question);
  box.setWindowTitle(kAppName);
  QPushButton *yes = nullptr;
  if (isClient) {
    box.setText(name.isEmpty() ? tr("Connect to this server?") : tr("Connect to %1?").arg(name));
    yes = box.addButton(tr("Yes"), QMessageBox::AcceptRole);
    box.setDefaultButton(yes);
    box.setEscapeButton(box.addButton(tr("No"), QMessageBox::RejectRole));
  } else {
    box.setText(
        name.isEmpty() ? tr("A computer is trying to connect") : tr("%1 is trying to connect").arg(name)
    );
    yes = box.addButton(tr("Accept"), QMessageBox::AcceptRole);
    box.setDefaultButton(yes);
    box.setEscapeButton(box.addButton(tr("Refuse"), QMessageBox::RejectRole));
  }

#ifdef Q_OS_MACOS
  forceAppActive();
#endif
  box.raise();
  box.activateWindow();
  box.exec();
  return box.clickedButton() == yes;
}

bool MainWindow::maybeHideToTray()
{
  if (!Settings::value(Settings::Gui::CloseToTray).toBool()) {
    return false;
  }

  if (Settings::value(Settings::Gui::CloseReminder).toBool()) {
    messages::showCloseReminder(this);
    Settings::setValue(Settings::Gui::CloseReminder, false);
  }
  Settings::setValue(Settings::Gui::WindowGeometry, geometry());
  qDebug() << "hiding to tray";
  hide();
  return true;
}

void MainWindow::quit()
{
  m_quitting = true;
  if (m_panel)
    m_panel->hide();
  close();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
  // Only closing the window hides it to the tray. Explicit quits also
  // generate spontaneous close events through macOS termination.
  if (event->spontaneous() && !m_quitting && isVisible() && maybeHideToTray()) {
    event->ignore();
    return;
  }

  if (m_saveOnExit) {
    Settings::setValue(Settings::Gui::WindowGeometry, geometry());
    Settings::setValue(Settings::Gui::AutoStartCore, m_coreProcess.isStarted());
  }
  qDebug() << "quitting application";

  // any connected dock view acitons will be triggered
  // disconnect them before accepting the event
  disconnect(m_logDock->toggleViewAction(), &QAction::toggled, nullptr, nullptr);

  event->accept();
  QApplication::quit();
}

void MainWindow::showFirstConnectedMessage()
{
  if (Settings::value(Settings::Gui::ShownFirstConnectedMessage).toBool())
    return;
  Settings::setValue(Settings::Gui::ShownFirstConnectedMessage, true);
  messages::showFirstConnectedMessage(this);
}

void MainWindow::updateStatus()
{
  using enum ProcessState;
  const auto connection = m_coreProcess.connectionState();
  const auto process = m_coreProcess.processState();
  const bool isServer = (m_coreProcess.mode() == CoreMode::Server);
  if (process == Stopped || process == Started) {
    updateNetworkInfo();
    ui->btnEditName->setVisible(process == Stopped);
  }
  m_statusBar->setStatus(connection, process, isServer);
  if (isServer && process == Started && m_clientConnectingTimer->isActive())
    m_statusBar->showServerConnecting();
}

void MainWindow::coreProcessStateChanged(ProcessState state)
{
  using enum ProcessState;
  updateStatus();
  updateAdvertising();
  updatePanel();
  if (state == Stopped)
    tryAutoStart();
  // the computer set as client connects to this new server
  if (state == Started && m_expectingClient && m_coreProcess.mode() == CoreMode::Server)
    showClientConnecting(kSetAsClientConnectTime);
  if (state == Started)
    m_expectingClient = false;
  if (state == Started) {
    qDebug() << "recording that core has started";
    Settings::setValue(Settings::Gui::AutoStartCore, true);
  }

  if (state == Started || state == Starting || state == RetryPending) {
    disconnect(ui->btnToggleCore, &QPushButton::clicked, m_actionStartCore, &QAction::trigger);
    connect(ui->btnToggleCore, &QPushButton::clicked, m_actionStopCore, &QAction::trigger, Qt::UniqueConnection);

    ui->btnRestartCore->setEnabled(true);
    m_actionStartCore->setVisible(false);
    m_actionRestartCore->setVisible(true);
    m_actionStopCore->setEnabled(true);

    if (state == Starting) {
      saveSettings();
    }

  } else {
    disconnect(ui->btnToggleCore, &QPushButton::clicked, m_actionStopCore, &QAction::trigger);
    connect(ui->btnToggleCore, &QPushButton::clicked, m_actionStartCore, &QAction::trigger, Qt::UniqueConnection);

    ui->btnRestartCore->setEnabled(canRunCore()); // restart also starts a stopped core
    m_actionStartCore->setVisible(true);
    m_actionRestartCore->setVisible(false);
    m_actionStopCore->setEnabled(false);
  }
  updateModeControlLabels();
}

void MainWindow::coreConnectionStateChanged(ConnectionState state)
{
  qDebug() << "core connection state changed:" << static_cast<int>(state);

  updateStatus();

  // always assume connection is not secure when connection changes
  // to anything except connected. the only way the padlock shows is
  // when the correct TLS version string is detected.
  if (state != ConnectionState::Connected) {
    secureSocket(false);
  } else if (isVisible()) {
    showFirstConnectedMessage();
  }
}

void MainWindow::updateFingerprintButton()
{
  m_statusBar->setBtnFingerprintVisible(TlsUtility::isEnabled() && !m_fingerprint.data.isEmpty());
}

void MainWindow::hide()
{
#ifdef Q_OS_MACOS
  macOSNativeHide();
#else
  QMainWindow::hide();
#endif
  m_actionRestore->setVisible(true);
  m_actionMinimize->setVisible(false);
}

void MainWindow::changeEvent(QEvent *e)
{
  QMainWindow::changeEvent(e);
  if (e->type() == QEvent::PaletteChange) {
    updateIconTheme();
    updateConfigureButtons();
    setWindowIcon(QIcon::fromTheme(kRevFqdnName));
    setTrayIcon();
  } else if (e->type() == QEvent::LanguageChange) {
    ui->retranslateUi(this);
    updateConfigureButtons();
    updateServerPicker();
    updateModeControlLabels();
    updateNetworkInfo();
    updateStatus();
    serverClientsChanged({});
    updateText();
  }
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
  if (obj != ui->lineEditName || event->type() != QEvent::KeyPress)
    return false;
  if (const auto keyEvent = static_cast<QKeyEvent *>(event); keyEvent->key() != Qt::Key_Escape)
    return false;
  ui->lineEditName->hide();
  ui->lblComputerName->show();
  ui->btnEditName->show();
  ui->lineEditName->setText(Settings::value(Settings::Core::ComputerName).toString());
  toggleCanRunCore(canRunCore());
  return true;
}

void MainWindow::updateText()
{
  m_menuFile->setTitle(tr("&File"));
  m_menuEdit->setTitle(tr("&Edit"));
  m_menuView->setTitle(tr("&View"));
  m_menuHelp->setTitle(tr("&Help"));

  m_actionMinimize->setText(tr("&Minimize to tray"));
  m_actionQuit->setText(tr("&Quit"));
  m_actionTrayQuit->setText(tr("&Quit"));
  //: %1 will be the replaced with the appname
  m_actionRestore->setText(tr("&Open %1").arg(kAppName));
  m_actionSettings->setText(tr("&Preferences"));
  m_actionStartCore->setText(tr("&Start"));
  m_actionRestartCore->setText(tr("Rest&art"));
  m_actionStopCore->setText(tr("S&top"));
  //: %1 will be the replaced with the appname
  m_actionAbout->setText(tr("About %1...").arg(kAppName));

  m_actionShowHelp->setText(tr("View &Help"));

  //: start / restart core shortcut
  m_actionStartCore->setShortcut(QKeySequence(tr("Ctrl+S")));
  m_actionRestartCore->setShortcut(QKeySequence(tr("Ctrl+S")));

  //: stop core shortcut
  m_actionStopCore->setShortcut(QKeySequence(tr("Ctrl+T")));

  if (deskflow::platform::isWindows()) {
    //: Quit shortcut
    m_actionQuit->setShortcut(QKeySequence(tr("Ctrl+Q")));
    m_actionTrayQuit->setShortcut(QKeySequence(tr("Ctrl+Q")));
  }
}

void MainWindow::showConfigureServer(const QString &message)
{
  ServerConfigDialog dialog(this, serverConfig());
  dialog.message(message);
  if ((dialog.exec() == QDialog::Accepted) && m_coreProcess.isStarted()) {
    m_coreProcess.restart();
  }
}

void MainWindow::showConfigureClient()
{
  ClientConfigDialog dialog(this);
  if ((dialog.exec() == QDialog::Accepted) && m_coreProcess.isStarted()) {
    m_coreProcess.restart();
  }
}

void MainWindow::secureSocket(bool secureSocket)
{
  m_secureSocket = secureSocket;
  updateSecurityIcon(m_statusBar->securityIconVisible());
}

void MainWindow::updateComputerName()
{
  const auto computerName = Settings::value(Settings::Core::ComputerName).toString();
  ui->lblComputerName->setText(computerName);
  ui->lineEditName->setText(computerName);
  m_serverConfig.updateServerName();
}

void MainWindow::showAndActivate()
{
  // the menu bar panel stands in for this window
  if (m_panel) {
    m_panel->show();
    return;
  }
  const auto wasVisible = isVisible();
#ifdef Q_OS_MACOS
  forceAppActive();
#endif
  showNormal();
  raise();
  activateWindow();
  m_actionRestore->setVisible(false);
  m_actionMinimize->setVisible(true);
  if (!wasVisible)
    restoreWindow();
}

void MainWindow::showHostNameEditor()
{
  ui->lineEditName->show();
  ui->lblComputerName->hide();
  ui->btnEditName->hide();
  toggleCanRunCore(false);
  ui->lineEditName->setFocus();
}

void MainWindow::setHostName()
{
  ui->lineEditName->hide();
  ui->lblComputerName->show();
  ui->btnEditName->show();
  toggleCanRunCore(canRunCore());

  QString text = ui->lineEditName->text();
  const auto computerName = Settings::value(Settings::Core::ComputerName).toString();

  if (text == computerName)
    return;

  const bool isServer = ui->rbModeServer->isChecked();
  bool existingComputer = false;
  if (isServer)
    existingComputer = serverConfig().computerExists(text);

  if (!ui->lineEditName->hasAcceptableInput() || text.isEmpty() || existingComputer) {
    blockSignals(true);
    ui->lineEditName->setText(computerName);
    blockSignals(false);

    const auto title = tr("Invalid Computer Name");
    QString body;
    if (existingComputer) {
      body = tr("Computer name already exists");
    } else {
      body =
          tr("The name you have chosen is invalid.\n\n"
             "Valid names:\n"
             "• Use letters and numbers\n"
             "• May also use _ or -\n"
             "• Are between 1 and 255 characters");
    }
    QMessageBox::information(this, title, body);
    return;
  }

  ui->lblComputerName->setText(ui->lineEditName->text());
  Settings::setValue(Settings::Core::ComputerName, ui->lineEditName->text());
  if (isServer)
    serverConfig().updateServerName();
  applyConfig();
}

QString MainWindow::trustedFingerprintDatabase() const
{
  const bool isClient = m_coreProcess.mode() == CoreMode::Client;
  return isClient ? Settings::tlsTrustedServersDb() : Settings::tlsTrustedClientsDb();
}

bool MainWindow::generateCertificate()
{
  const auto certificate = Settings::value(Settings::Security::Certificate).toString();
  if (!QFile::exists(certificate) && !TlsUtility::generateCertificate()) {
    return false;
  }

  m_fingerprint = {QCryptographicHash::Sha256, TlsUtility::certFingerprint()};
  updateFingerprintButton();
  return true;
}

void MainWindow::serverClientsChanged(const QStringList &clients)
{
  // one more client: the one that was connecting is connected
  if (clients.size() > m_connectedClients.size())
    m_clientConnectingTimer->stop();
  m_connectedClients = clients;
  updatePanel();
  if (m_coreProcess.mode() != CoreMode::Server || !m_coreProcess.isStarted())
    return;
  if (m_clientConnectingTimer->isActive())
    m_statusBar->showServerConnecting();
  else
    m_statusBar->setServerClients(clients);
}

void MainWindow::showClientConnecting(int ms)
{
  if (m_coreProcess.mode() != CoreMode::Server || !m_coreProcess.isStarted())
    return;
  if (!m_clientConnectingTimer->isActive() || m_clientConnectingTimer->remainingTime() < ms)
    m_clientConnectingTimer->start(ms);
  m_statusBar->showServerConnecting();
}

void MainWindow::clientConnectingEnded()
{
  if (m_coreProcess.mode() != CoreMode::Server || !m_coreProcess.isStarted())
    return;
  m_statusBar->setServerClients(m_connectedClients);
}

void MainWindow::daemonIpcClientConnectionFailed()
{
  if (deskflow::gui::messages::showDaemonOffline(this)) {
    m_coreProcess.retryDaemon();
  }
}

void MainWindow::toggleCanRunCore(bool enableButtons)
{
  const bool isStarted = m_coreProcess.isStarted();
  ui->btnToggleCore->setEnabled(enableButtons);
  ui->btnRestartCore->setEnabled(enableButtons);
  m_actionStartCore->setEnabled(enableButtons);
  m_actionStopCore->setEnabled(enableButtons && isStarted);
}

void MainWindow::updateConfigureButtons()
{
  // icon-only gear buttons (the text comes back on retranslate)
  const auto icon = orbit::icons::gear(palette().color(QPalette::ButtonText));
  for (auto *button : {ui->btnConfigureServer, ui->btnConfigureClient}) {
    button->setIcon(icon);
    button->setIconSize(QSize(18, 18));
    button->setText(QString());
    button->setFixedSize(32, 32);
  }
  ui->btnConfigureServer->setToolTip(tr("Configure server"));
  ui->btnConfigureClient->setToolTip(tr("Configure client"));
}

void MainWindow::updateAdvertising()
{
  const bool serving =
      m_coreProcess.mode() == CoreMode::Server && m_coreProcess.processState() == ProcessState::Started;
  if (!serving) {
    m_advertiser->stop();
    m_remoteListener->stop();
    return;
  }
  if (!m_advertiser->isActive()) {
    const auto name = Settings::value(Settings::Core::ComputerName).toString();
    const auto port = static_cast<quint16>(Settings::value(Settings::Core::Port).toInt());
    // other Orbits can ask this server to become their client
    if (!m_remoteListener->isListening() && generateCertificate())
      m_remoteListener->start(
          orbit::remote::controlPort(port), Settings::value(Settings::Security::Certificate).toString()
      );
    m_advertiser->start(name, port, m_remoteListener->port());
  }
}

void MainWindow::updateServerPicker()
{
  enum Kind
  {
    Found,
    Current,
    Placeholder,
    Manual
  };
  constexpr int kKindRole = Qt::UserRole;
  constexpr int kAddressRole = Qt::UserRole + 1;

  const auto current = ui->lineHostname->text().trimmed();
  const auto ownName = Settings::value(Settings::Core::ComputerName).toString();

  QSignalBlocker blocker(m_serverPicker);
  m_serverPicker->clear();
  int selected = -1;
  QString serverName;

  for (const auto &server : m_browser->servers()) {
    if (server.name.compare(ownName, Qt::CaseInsensitive) == 0)
      continue;
    m_serverPicker->addItem(server.name);
    const int row = m_serverPicker->count() - 1;
    m_serverPicker->setItemData(row, Found, kKindRole);
    m_serverPicker->setItemData(row, server.address(), kAddressRole);
    auto details = server.host;
    if (!server.addresses.isEmpty())
      details += QStringLiteral(" (%1)").arg(server.addresses.join(QStringLiteral(", ")));
    m_serverPicker->setItemData(row, details, Qt::ToolTipRole);
    if (selected < 0 && server.matches(current)) {
      selected = row;
      serverName = server.name;
    }
  }
  const bool foundAny = m_serverPicker->count() > 0;

  // keep showing the configured server while it is not found on the network
  if (selected < 0 && !current.isEmpty() && !m_manualHostEntry) {
    m_serverPicker->insertItem(0, current);
    m_serverPicker->setItemData(0, Current, kKindRole);
    m_serverPicker->setItemData(0, current, kAddressRole);
    m_serverPicker->setItemData(0, tr("Not found on the network right now"), Qt::ToolTipRole);
    selected = 0;
  }

  if (!foundAny) {
    m_serverPicker->addItem(tr("Searching for Macs…"));
    const int row = m_serverPicker->count() - 1;
    m_serverPicker->setItemData(row, Placeholder, kKindRole);
    if (auto *model = qobject_cast<QStandardItemModel *>(m_serverPicker->model()))
      model->item(row)->setEnabled(false);
    if (selected < 0 && !m_manualHostEntry)
      selected = row;
  }

  m_serverPicker->insertSeparator(m_serverPicker->count());
  m_serverPicker->addItem(tr("Enter address…"));
  const int manualRow = m_serverPicker->count() - 1;
  m_serverPicker->setItemData(manualRow, Manual, kKindRole);
  if (m_manualHostEntry)
    selected = manualRow;

  m_serverPicker->setCurrentIndex(selected);
  ui->lineHostname->setVisible(m_manualHostEntry);
  m_statusBar->setServerName(serverName);
  updatePanel();
}

void MainWindow::startCoreIfReady()
{
  // there is no start button: the server starts when its mode is chosen, a
  // client as soon as it has a server
  m_startWhenStopped = true;
  QTimer::singleShot(0, this, &MainWindow::tryAutoStart);
}

void MainWindow::tryAutoStart()
{
  if (!m_startWhenStopped)
    return;
  const auto mode = m_coreProcess.mode();
  const bool ready =
      mode == CoreMode::Server || (mode == CoreMode::Client && !ui->lineHostname->text().trimmed().isEmpty());
  const auto state = m_coreProcess.processState();
  if (!ready || state != ProcessState::Stopping)
    m_startWhenStopped = false; // only wait while the previous core stops
  if (ready && state == ProcessState::Stopped)
    startCore();
}

void MainWindow::serverPicked(int index)
{
  constexpr int kKindRole = Qt::UserRole;
  constexpr int kAddressRole = Qt::UserRole + 1;
  enum Kind
  {
    Found,
    Current,
    Placeholder,
    Manual
  };

  const auto kind = m_serverPicker->itemData(index, kKindRole).toInt();
  if (kind == Manual) {
    m_manualHostEntry = true;
    ui->lineHostname->setVisible(true);
    ui->lineHostname->setFocus();
    ui->lineHostname->selectAll();
    return;
  }
  if (kind != Found)
    return;

  m_manualHostEntry = false;
  connectToServer(m_serverPicker->itemData(index, kAddressRole).toString());
}

void MainWindow::connectToServer(const QString &address)
{
  const bool changed = address != ui->lineHostname->text().trimmed();
  if (changed)
    ui->lineHostname->setText(address); // saves it and updates the core's address
  updateServerPicker();

  // picking a server connects to it
  if (m_coreProcess.processState() == ProcessState::Stopped)
    startCore();
  else if (changed)
    m_coreProcess.restart();
}

void MainWindow::setAsClient(const QString &address)
{
  if (m_remoteSender->isBusy())
    return;

  const auto servers = m_browser->servers();
  const auto found = std::find_if(servers.cbegin(), servers.cend(), [&](const auto &server) {
    return server.address() == address;
  });
  m_setAsClientAddress = address;
  m_setAsClientError.clear();
  if (found == servers.cend() || found->controlPort == 0) {
    setAsClientFinished(false, tr("This Mac's Orbit can't be set as client: update it first"));
    return;
  }
  if (!generateCertificate()) {
    setAsClientFinished(false, tr("This Mac has no certificate"));
    return;
  }
  m_setAsClientName = found->name;
  updatePanel();

  // the other Mac connects here by name, so a new address doesn't matter
  const auto port = static_cast<quint16>(Settings::value(Settings::Core::Port).toInt());
  auto server = orbit::remote::localHostName();
  if (constexpr quint16 kDefaultPort = 24800; port != kDefaultPort)
    server += QStringLiteral(":%1").arg(port);
  const auto host = found->addresses.isEmpty() ? found->host : found->addresses.first();
  m_remoteSender->sendBecomeClient(
      host, found->controlPort, Settings::value(Settings::Core::ComputerName).toString(), server,
      Settings::value(Settings::Security::Certificate).toString()
  );
}

void MainWindow::setAsClientFinished(bool ok, const QString &error)
{
  if (!ok) {
    m_setAsClientError = error.isEmpty() ? tr("It refused") : error;
    updatePanel();
    // the error goes after a while, unless another try replaced it
    const auto address = m_setAsClientAddress;
    QTimer::singleShot(10000, this, [this, address] {
      if (m_remoteSender->isBusy() || m_setAsClientAddress != address)
        return;
      m_setAsClientAddress.clear();
      m_setAsClientError.clear();
      updatePanel();
    });
    return;
  }

  qInfo().noquote() << m_setAsClientName << "becomes a client of this Mac";
  m_setAsClientAddress.clear();
  m_setAsClientError.clear();

  // asked from here: trust it as a client, so it doesn't have to be accepted
  if (const auto fingerprint = m_remoteSender->peerFingerprint(); fingerprint.isValid()) {
    FingerprintDatabase db;
    db.read(Settings::tlsTrustedClientsDb());
    if (!db.isTrusted(fingerprint)) {
      db.addTrusted(fingerprint);
      if (!db.write(Settings::tlsTrustedClientsDb()))
        qWarning().noquote() << "unable to write fingerprint to db:" << Settings::tlsTrustedClientsDb();
    }
  }

  // this Mac shares its keyboard and mouse with it now; it connects once
  // its server has stopped
  m_expectingClient = true;
  if (m_coreProcess.mode() != CoreMode::Server)
    ui->rbModeServer->setChecked(true);
  else if (m_coreProcess.isStarted())
    showClientConnecting(kSetAsClientConnectTime);
  updatePanel();
}

void MainWindow::handleBecomeClientRequest(orbit::remote::Request *request)
{
  QPointer<orbit::remote::Request> guard(request);
  const auto ownName = Settings::value(Settings::Core::ComputerName).toString();
  const auto name = request->name().isEmpty() ? request->server() : request->name();
  const auto fingerprint = request->fingerprint();

  // a server this Mac already trusts may take it; another one is asked for
  FingerprintDatabase db;
  db.read(Settings::tlsTrustedServersDb());
  if (!db.isTrusted(fingerprint)) {
    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(kAppName);
    box.setText(tr("Use the keyboard and mouse of %1?").arg(name));
    box.setInformativeText(tr("%1 asks this Mac to become its client.").arg(name));
    auto *allow = box.addButton(tr("Allow"), QMessageBox::AcceptRole);
    box.setDefaultButton(allow);
    box.setEscapeButton(box.addButton(tr("Refuse"), QMessageBox::RejectRole));
#ifdef Q_OS_MACOS
    forceAppActive();
#endif
    box.raise();
    box.activateWindow();
    box.exec();
    if (!guard)
      return; // the other Mac gave up meanwhile
    if (box.clickedButton() != allow) {
      qInfo().noquote() << "refused to become a client of" << name;
      request->reply(false, tr("Refused on %1").arg(ownName));
      return;
    }
    db.addTrusted(fingerprint);
    if (!db.write(Settings::tlsTrustedServersDb())) {
      qCritical().noquote() << "unable to write fingerprint to db:" << Settings::tlsTrustedServersDb();
      request->reply(false, tr("%1 could not save the trust").arg(ownName));
      return;
    }
  }

  const auto server = request->server();
  request->reply(true);
  // after the answer has gone: becoming a client stops this server
  QTimer::singleShot(0, this, [this, server] { becomeClientOf(server); });
}

void MainWindow::becomeClientOf(const QString &server)
{
  qInfo().noquote() << "becoming a client of" << server;
  m_manualHostEntry = false;
  if (m_coreProcess.mode() == CoreMode::Client) {
    connectToServer(server);
    return;
  }
  // the client starts with this address once the server has stopped
  ui->lineHostname->setText(server);
  ui->rbModeClient->setChecked(true);
}

void MainWindow::updatePanel()
{
  // coalesce: many changes come together
  if (m_panelUpdatePending || !m_panel)
    return;
  m_panelUpdatePending = true;
  QTimer::singleShot(0, this, &MainWindow::sendPanelState);
}

void MainWindow::sendPanelState()
{
  m_panelUpdatePending = false;
  using IconState = OrbitStatusIcon::State;

  QJsonObject state;
  const auto mode = m_coreProcess.mode();
  state[QStringLiteral("mode")] = mode == CoreMode::Server   ? QStringLiteral("server")
                                  : mode == CoreMode::Client ? QStringLiteral("client")
                                                             : QStringLiteral("none");
  switch (m_statusBar->statusIconState()) {
  case IconState::Idle:
    state[QStringLiteral("phase")] = QStringLiteral("idle");
    break;
  case IconState::Waiting:
    state[QStringLiteral("phase")] = QStringLiteral("waiting");
    break;
  case IconState::Connecting:
    state[QStringLiteral("phase")] = QStringLiteral("connecting");
    break;
  case IconState::Connected:
    state[QStringLiteral("phase")] = QStringLiteral("connected");
    break;
  }
  state[QStringLiteral("statusText")] = m_statusBar->statusText();
  state[QStringLiteral("animatedMenuBarIcon")] = Settings::value(Settings::Gui::AnimatedMenuBarIcon).toBool();

  state[QStringLiteral("server")] = QJsonObject{
      {QStringLiteral("trackpadGestures"), Settings::value(Settings::Server::TrackpadGestures).toBool()},
      {QStringLiteral("fileDragDrop"), Settings::value(Settings::Server::EnableFileDragDrop).toBool()},
      {QStringLiteral("clipboard"), Settings::value(Settings::Server::EnableClipboard).toBool()},
      {QStringLiteral("touchIdInMenuBar"), Settings::value(Settings::Server::TouchIdInMenuBar).toBool()},
  };
  const auto host = ui->lineHostname->text().trimmed();
  QJsonObject client{
      {QStringLiteral("invertHorizontalScroll"), Settings::value(Settings::Client::InvertXScroll).toBool()},
      {QStringLiteral("invertVerticalScroll"), Settings::value(Settings::Client::InvertYScroll).toBool()},
      {QStringLiteral("host"), host},
  };
#ifdef Q_OS_MACOS
  client[QStringLiteral("screenUnlock")] = screenunlock::hasPassword();
  if (!m_screenUnlockError.isEmpty())
    client[QStringLiteral("screenUnlockError")] = m_screenUnlockError;
#endif
  state[QStringLiteral("client")] = client;

  const auto ownName = Settings::value(Settings::Core::ComputerName).toString();
  QJsonArray servers;
  for (const auto &server : m_browser->servers()) {
    if (server.name.compare(ownName, Qt::CaseInsensitive) == 0)
      continue;
    auto detail = server.host;
    if (!server.addresses.isEmpty())
      detail += QStringLiteral(" (%1)").arg(server.addresses.join(QStringLiteral(", ")));
    servers.append(QJsonObject{
        {QStringLiteral("name"), server.name},
        {QStringLiteral("address"), server.address()},
        {QStringLiteral("detail"), detail},
        {QStringLiteral("canSetAsClient"), server.controlPort != 0},
    });
    if (!state.contains(QStringLiteral("selectedServer")) && server.matches(host))
      state[QStringLiteral("selectedServer")] = server.address();
  }
  state[QStringLiteral("servers")] = servers;
  if (!m_setAsClientAddress.isEmpty()) {
    QJsonObject request{{QStringLiteral("address"), m_setAsClientAddress}};
    if (!m_setAsClientError.isEmpty())
      request[QStringLiteral("error")] = m_setAsClientError;
    state[QStringLiteral("setAsClient")] = request;
  }

  const int columns = Settings::value(Settings::Server::GridWidth).toInt();
  const int rows = Settings::value(Settings::Server::GridHeight).toInt();
  QJsonArray computers;
  const auto &list = std::as_const(m_serverConfig).computers();
  for (int i = 0; i < list.size(); ++i) {
    const auto &computer = list[i];
    if (computer.isNull())
      continue;
    const bool isServer = computer.name() == m_serverConfig.getServerName();
    computers.append(QJsonObject{
        {QStringLiteral("name"), computer.name()},
        {QStringLiteral("column"), i % columns},
        {QStringLiteral("row"), i / columns},
        {QStringLiteral("isServer"), isServer},
        {QStringLiteral("isConnected"), m_connectedClients.contains(computer.name())},
    });
  }
  state[QStringLiteral("arrangement")] = QJsonObject{
      {QStringLiteral("columns"), columns},
      {QStringLiteral("rows"), rows},
      {QStringLiteral("computers"), computers},
  };

  m_panel->setState(state);
}

void MainWindow::handlePanelAction(const QString &name, const QJsonObject &arguments)
{
  if (name == QStringLiteral("selectMode")) {
    const auto mode = arguments.value(QStringLiteral("mode")).toString();
    if (mode == QStringLiteral("server"))
      ui->rbModeServer->setChecked(true);
    else if (mode == QStringLiteral("client"))
      ui->rbModeClient->setChecked(true);
  } else if (name == QStringLiteral("setOption")) {
    static const QHash<QString, QString> keys = {
        {QStringLiteral("trackpadGestures"), Settings::Server::TrackpadGestures},
        {QStringLiteral("fileDragDrop"), Settings::Server::EnableFileDragDrop},
        {QStringLiteral("clipboard"), Settings::Server::EnableClipboard},
        {QStringLiteral("touchIdInMenuBar"), Settings::Server::TouchIdInMenuBar},
        {QStringLiteral("invertHorizontalScroll"), Settings::Client::InvertXScroll},
        {QStringLiteral("invertVerticalScroll"), Settings::Client::InvertYScroll},
    };
    const auto option = arguments.value(QStringLiteral("key")).toString();
    if (option == QStringLiteral("animatedMenuBarIcon")) {
      Settings::setValue(Settings::Gui::AnimatedMenuBarIcon, arguments.value(QStringLiteral("value")).toBool());
      Settings::save();
      updatePanel(); // the panel's menu bar icon follows this setting
      return;
    }
    const auto key = keys.value(option);
    if (key.isEmpty())
      return;
    Settings::setValue(key, arguments.value(QStringLiteral("value")).toBool());
    Settings::save();
    updatePanel();
    // the core takes its options again without disconnecting
    m_coreProcess.reloadConfig();
  } else if (name == QStringLiteral("pickServer")) {
    m_manualHostEntry = false;
    connectToServer(arguments.value(QStringLiteral("address")).toString().trimmed());
  } else if (name == QStringLiteral("setAsClient")) {
    setAsClient(arguments.value(QStringLiteral("address")).toString());
  } else if (name == QStringLiteral("moveComputer")) {
    const int columns = Settings::value(Settings::Server::GridWidth).toInt();
    const int rows = Settings::value(Settings::Server::GridHeight).toInt();
    const auto index = [&](const QString &column, const QString &row) {
      const int c = arguments.value(column).toInt(-1);
      const int r = arguments.value(row).toInt(-1);
      return (c < 0 || r < 0 || c >= columns || r >= rows) ? -1 : r * columns + c;
    };
    const int from = index(QStringLiteral("fromColumn"), QStringLiteral("fromRow"));
    const int to = index(QStringLiteral("toColumn"), QStringLiteral("toRow"));
    // move there, swapping with the computer already in that place
    if (from == to || !m_serverConfig.moveComputer(from, to))
      return;
    m_serverConfig.commit();
    Settings::save();
    updatePanel();
    // the server takes the new arrangement without dropping the clients
    m_coreProcess.reloadConfig();
#ifdef Q_OS_MACOS
  } else if (name == QStringLiteral("setScreenUnlockPassword")) {
    // the client types it into its lock screen after Touch ID on the server
    QString error;
    auto password = arguments.value(QStringLiteral("password")).toString();
    m_screenUnlockError = screenunlock::storePassword(password, error) ? QString() : error;
    password.fill(QChar(u'\0'));
    updatePanel();
  } else if (name == QStringLiteral("removeScreenUnlockPassword")) {
    screenunlock::removePassword();
    m_screenUnlockError.clear();
    updatePanel();
#endif
  } else if (name == QStringLiteral("restart")) {
    resetCore();
  } else if (name == QStringLiteral("quit")) {
    quit();
  }
}

void MainWindow::remoteHostChanged(const QString &newRemoteHost)
{
  m_coreProcess.setAddress(newRemoteHost);
  toggleCanRunCore(!newRemoteHost.isEmpty() && ui->rbModeClient->isChecked());
  if (newRemoteHost.isEmpty()) {
    Settings::setValue(Settings::Client::RemoteHost);
  } else {
    Settings::setValue(Settings::Client::RemoteHost, newRemoteHost);
  }
}

void MainWindow::updateIpLabel(const QStringList &addresses)
{
  const auto mode = m_coreProcess.mode();
  if (mode == CoreMode::None) {
    return;
  }

  static const auto colorText = QStringLiteral(R"(<span style="color:%1;">%2</span>)");
  const bool serverStarted = m_coreProcess.isStarted();
  const bool fixedIP = !Settings::value(Settings::Core::Interface).toString().isEmpty();

  if (!fixedIP && addresses.isEmpty() && !serverStarted || (serverStarted && m_serverStartSuggestedIP.isEmpty())) {
    ui->lblIpAddresses->setText(colorText.arg(palette().linkVisited().color().name(), tr("No IP Detected")));
    ui->lblIpAddresses->setToolTip(tr("Unable to detect an IP address. Check your network connection is active."));
    return;
  }

  QString labelText = fixedIP ? tr("Using IP: ") : tr("Suggested IP: ");
  QString toolTipText = tr("<p>If connecting via the hostname fails, try %1</p>");

  const bool filterIpList = (serverStarted || fixedIP);
  const QRegularExpression ipListFilter(filterIpList ? QStringLiteral("(%1)").arg(m_serverStartIPs.join("|")) : "");
  const QStringList ipList = addresses.filter(ipListFilter);

  bool IPValid = true;
  if (filterIpList && (m_serverStartSuggestedIP != m_currentIpAddress) || !ipList.contains(m_serverStartSuggestedIP)) {
    IPValid = !ipList.isEmpty();
  }

  if (IPValid) {
    m_currentIpAddress = ipList.first();
    labelText.append(m_currentIpAddress);
  } else {
    labelText.append(colorText.arg(palette().linkVisited().color().name(), m_serverStartSuggestedIP));
    toolTipText.append(tr("\nA bound IP is now invalid, you may need to restart the server."));
  }

  if (ipList.count() < 2 || fixedIP) {
    toolTipText = toolTipText.arg(tr("the suggested IP."));
  } else {
    toolTipText = toolTipText.arg(tr("one of the following IPs:<br/>%1").arg(ipList.join("<br/>")));
  }

  ui->lblIpAddresses->setText(labelText);
  ui->lblIpAddresses->setToolTip(mode == CoreMode::Server ? toolTipText : QString());
}

void MainWindow::updateTimeoutDelay(int newDelay)
{
  m_statusBar->setConnectionInterval(newDelay);
}

void MainWindow::setHelpFilePath()
{
  const QString appPath = QCoreApplication::applicationDirPath();
  auto buildPath = QString("%1/../docs/HelpMain.md").arg(appPath);
  auto installPath = QString("%1/../share/doc/%2/HelpMain.md").arg(appPath, kAppId);
  if (deskflow::platform::isMac()) {
    installPath = QString("%1/../Resources/docs/HelpMain.md").arg(appPath);
    buildPath = QString("%1/../../../../docs/HelpMain.md").arg(appPath);
  } else if (deskflow::platform::isWindows()) {
    installPath = QString("%1/docs/HelpMain.md").arg(appPath);
  }

  buildPath = QDir::cleanPath(buildPath);
  installPath = QDir::cleanPath(installPath);

  if (QFile::exists(installPath))
    m_helpPath = QUrl::fromLocalFile(installPath);
  else if (QFile::exists(buildPath))
    m_helpPath = QUrl::fromLocalFile(buildPath);
  else
    m_helpPath = kUrlWiki;
}

void MainWindow::showHelpViewer() const
{
  if (m_helpPath.isLocalFile()) {
    HelpDialog dialogHelp(this->centralWidget(), m_helpPath);
    dialogHelp.exec();
  } else {
    QDesktopServices::openUrl(m_helpPath);
  }
}

bool MainWindow::canRunCore() const
{
  const auto mode = m_coreProcess.mode();
  const bool isServer = mode == Settings::CoreMode::Server;
  const bool isClient = mode == Settings::CoreMode::Client;
  return ((isServer || isClient) && (isClient && !ui->lineHostname->text().isEmpty()) || isServer);
}
