#include "MainWindow.hpp"

#include "CodeEditor.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QComboBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextStream>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

#include "pl0/PCode.hpp"
#include "pl0/SymbolTable.hpp"
#include "pl0/Token.hpp"

namespace {

constexpr int kBeginLineRole = Qt::UserRole;
constexpr int kBeginColumnRole = Qt::UserRole + 1;
constexpr int kEndLineRole = Qt::UserRole + 2;
constexpr int kEndColumnRole = Qt::UserRole + 3;

QTableWidget* makeTable(const QStringList& headers, QWidget* parent) {
  auto* table = new QTableWidget(parent);
  table->setColumnCount(headers.size());
  table->setHorizontalHeaderLabels(headers);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setSelectionMode(QAbstractItemView::SingleSelection);
  table->setAlternatingRowColors(true);
  table->setShowGrid(false);
  table->verticalHeader()->hide();
  table->verticalHeader()->setDefaultSectionSize(30);
  table->horizontalHeader()->setHighlightSections(false);
  table->horizontalHeader()->setStretchLastSection(true);
  return table;
}

QFrame* makeCard(const QString& title, QWidget* content, QWidget* parent) {
  auto* card = new QFrame(parent);
  card->setObjectName(QStringLiteral("card"));
  auto* layout = new QVBoxLayout(card);
  layout->setContentsMargins(14, 12, 14, 14);
  layout->setSpacing(10);
  auto* heading = new QLabel(title, card);
  heading->setObjectName(QStringLiteral("cardTitle"));
  layout->addWidget(heading);
  layout->addWidget(content, 1);
  return card;
}

QString diagnosticLevelName(pl0::DiagnosticLevel level) {
  switch (level) {
    case pl0::DiagnosticLevel::Error: return QObject::tr("错误");
    case pl0::DiagnosticLevel::Warning: return QObject::tr("警告");
    case pl0::DiagnosticLevel::Note: return QObject::tr("提示");
  }
  return QObject::tr("未知");
}

QColor diagnosticColor(pl0::DiagnosticLevel level) {
  switch (level) {
    case pl0::DiagnosticLevel::Error: return QColor(QStringLiteral("#DC2626"));
    case pl0::DiagnosticLevel::Warning: return QColor(QStringLiteral("#D97706"));
    case pl0::DiagnosticLevel::Note: return QColor(QStringLiteral("#2563EB"));
  }
  return QColor(QStringLiteral("#64748B"));
}

QString parseTraceKindName(pl0::ParseTraceKind kind) {
  switch (kind) {
    case pl0::ParseTraceKind::Enter: return QObject::tr("进入");
    case pl0::ParseTraceKind::Consume: return QObject::tr("消费");
    case pl0::ParseTraceKind::Exit: return QObject::tr("完成");
    case pl0::ParseTraceKind::Error: return QObject::tr("错误");
    case pl0::ParseTraceKind::Recover: return QObject::tr("恢复");
  }
  return QObject::tr("未知");
}

QString markdownCell(QString text) {
  text.replace(QLatin1Char('|'), QStringLiteral("\\|"));
  text.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
  return text;
}

QString tableAsMarkdown(const QTableWidget* table) {
  QString result;
  QTextStream stream(&result);
  stream << "|";
  for (int column = 0; column < table->columnCount(); ++column) {
    const auto* header = table->horizontalHeaderItem(column);
    stream << ' ' << markdownCell(header ? header->text() : QString()) << " |";
  }
  stream << "\n|";
  for (int column = 0; column < table->columnCount(); ++column) stream << " --- |";
  stream << '\n';
  for (int row = 0; row < table->rowCount(); ++row) {
    stream << "|";
    for (int column = 0; column < table->columnCount(); ++column) {
      const auto* item = table->item(row, column);
      stream << ' ' << markdownCell(item ? item->text() : QString()) << " |";
    }
    stream << '\n';
  }
  if (table->rowCount() == 0) stream << "\n_无记录_\n";
  return result;
}

const QString kExpressionExample = QStringLiteral(
    "const base = 2;\n"
    "var x;\n"
    "begin\n"
    "  x := base + 3 * 4;\n"
    "  writeln(x)\n"
    "end.\n");

const QString kLoopExample = QStringLiteral(
    "var i, sum;\n"
    "begin\n"
    "  i := 1;\n"
    "  sum := 0;\n"
    "  while i <= 5 do\n"
    "  begin\n"
    "    sum += i;\n"
    "    i++\n"
    "  end;\n"
    "  writeln(sum)\n"
    "end.\n");

const QString kProcedureExample = QStringLiteral(
    "var x;\n"
    "procedure increase;\n"
    "begin\n"
    "  x := x + 1;\n"
    "  writeln(x)\n"
    "end;\n"
    "begin\n"
    "  x := 40;\n"
    "  call increase;\n"
    "  call increase\n"
    "end.\n");

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  setupUi();
  setupActions();
  setupConnections();
  applyTheme();

  loadingSource_ = true;
  sourceEdit_->setPlainText(kExpressionExample);
  loadingSource_ = false;
  documentDirty_ = false;
  setState(AppState::Idle, tr("载入示例后点击“编译”开始分析"));
  updateWindowTitle();
}

void MainWindow::setupUi() {
  resize(1480, 900);
  setMinimumSize(1120, 720);
  setWindowTitle(tr("PL/0 可视化实验室"));

  auto* root = new QWidget(this);
  auto* rootLayout = new QVBoxLayout(root);
  rootLayout->setContentsMargins(14, 12, 14, 12);
  rootLayout->setSpacing(12);

  auto* intro = new QFrame(root);
  intro->setObjectName(QStringLiteral("intro"));
  auto* introLayout = new QHBoxLayout(intro);
  introLayout->setContentsMargins(18, 12, 18, 12);
  auto* titleBox = new QVBoxLayout;
  auto* title = new QLabel(tr("PL/0 可视化实验室"), intro);
  title->setObjectName(QStringLiteral("appTitle"));
  auto* subtitle = new QLabel(tr("从源代码到 P-Code，再到运行栈的完整编译过程"), intro);
  subtitle->setObjectName(QStringLiteral("subtitle"));
  titleBox->addWidget(title);
  titleBox->addWidget(subtitle);
  introLayout->addLayout(titleBox);
  introLayout->addStretch();
  auto* modeLabel = new QLabel(tr("语言模式"), intro);
  modeLabel->setObjectName(QStringLiteral("subtitle"));
  introLayout->addWidget(modeLabel);
  languageModeCombo_ = new QComboBox(intro);
  languageModeCombo_->setObjectName(QStringLiteral("languageModeCombo"));
  languageModeCombo_->addItems({tr("Baseline 教材基线"), tr("Extended 扩展模式")});
  languageModeCombo_->setCurrentIndex(1);
  languageModeCombo_->setMinimumWidth(170);
  languageModeCombo_->setToolTip(
      tr("Baseline 仅接受教材标准语法；Extended 可使用数组、布尔值、repeat、else 等扩展"));
  introLayout->addWidget(languageModeCombo_);
  stateBadge_ = new QLabel(tr("就绪"), intro);
  stateBadge_->setObjectName(QStringLiteral("stateBadge"));
  introLayout->addWidget(stateBadge_);
  rootLayout->addWidget(intro);

  auto* mainSplit = new QSplitter(Qt::Horizontal, root);
  mainSplit->setChildrenCollapsible(false);

  auto* editorPane = new QWidget(mainSplit);
  auto* editorLayout = new QVBoxLayout(editorPane);
  editorLayout->setContentsMargins(0, 0, 0, 0);
  editorLayout->setSpacing(8);
  auto* editorHeader = new QHBoxLayout;
  auto* sourceTitle = new QLabel(tr("源代码"), editorPane);
  sourceTitle->setObjectName(QStringLiteral("sectionTitle"));
  sourceHint_ = new QLabel(tr("内存示例 · PL/0"), editorPane);
  sourceHint_->setObjectName(QStringLiteral("muted"));
  editorHeader->addWidget(sourceTitle);
  editorHeader->addStretch();
  editorHeader->addWidget(sourceHint_);
  editorLayout->addLayout(editorHeader);
  sourceEdit_ = new CodeEditor(editorPane);
  sourceEdit_->setPlaceholderText(tr("在此输入 PL/0 源程序…"));
  sourceEdit_->setObjectName(QStringLiteral("sourceEditor"));
  QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  mono.setPointSize(11);
  sourceEdit_->setFont(mono);
  sourceEdit_->setLineNumberFont(mono);
  sourceEdit_->setTabStopDistance(4 * sourceEdit_->fontMetrics().horizontalAdvance(' '));
  editorLayout->addWidget(sourceEdit_, 1);

  auto* rightSplit = new QSplitter(Qt::Vertical, mainSplit);
  rightSplit->setChildrenCollapsible(false);

  analysisTabs_ = new QTabWidget(rightSplit);
  analysisTabs_->setDocumentMode(true);
  tokensTable_ = makeTable({tr("#"), tr("词素"), tr("种类"), tr("值"), tr("位置")}, analysisTabs_);
  tokensTable_->setColumnWidth(0, 52);
  tokensTable_->setColumnWidth(1, 130);
  tokensTable_->setColumnWidth(2, 130);
  tokensTable_->setColumnWidth(3, 90);
  analysisTabs_->addTab(tokensTable_, tr("词法单元"));

  symbolsTable_ = makeTable({tr("名称"), tr("种类"), tr("类型"), tr("层级"), tr("地址"), tr("大小"), tr("声明")}, analysisTabs_);
  analysisTabs_->addTab(symbolsTable_, tr("符号表"));

  pcodeTable_ = makeTable({tr("地址"), tr("操作码"), tr("L"), tr("A / 子操作"), tr("源码")}, analysisTabs_);
  pcodeTable_->setColumnWidth(0, 72);
  pcodeTable_->setColumnWidth(1, 110);
  pcodeTable_->setColumnWidth(2, 64);
  pcodeTable_->setToolTip(tr("单击跳转到对应源码；双击任意一行设置或取消断点"));
  analysisTabs_->addTab(pcodeTable_, tr("P-Code"));

  parseTracePanel_ = new QWidget(analysisTabs_);
  auto* parseLayout = new QVBoxLayout(parseTracePanel_);
  parseLayout->setContentsMargins(4, 4, 4, 4);
  auto* parseControls = new QHBoxLayout;
  auto addParseButton = [&](const QString& text, const char* name, auto slot) {
    auto* button = new QPushButton(text, parseTracePanel_);
    button->setObjectName(QString::fromLatin1(name));
    parseControls->addWidget(button);
    connect(button, &QPushButton::clicked, this, slot);
    return button;
  };
  parsePrevious_ = addParseButton(tr("上一步"), "parsePrevious", &MainWindow::previousParseEvent);
  parseNext_ = addParseButton(tr("下一步"), "parseNext", &MainWindow::nextParseEvent);
  parsePlay_ = addParseButton(tr("播放"), "parsePlay", &MainWindow::playParseTrace);
  parsePause_ = addParseButton(tr("暂停"), "parsePause", &MainWindow::pauseParseTrace);
  parseReset_ = addParseButton(tr("重置"), "parseReset", &MainWindow::resetParseTrace);
  parseFull_ = addParseButton(tr("全部"), "parseFull", &MainWindow::showFullParseTrace);
  parseSpeed_ = new QComboBox(parseTracePanel_);
  parseSpeed_->addItem(tr("慢速"), 1200);
  parseSpeed_->addItem(tr("正常"), 600);
  parseSpeed_->addItem(tr("快速"), 150);
  parseSpeed_->setCurrentIndex(1);
  parseControls->addWidget(parseSpeed_);
  parseLayout->addLayout(parseControls);
  parseProgress_ = new QLabel(parseTracePanel_);
  parseProgress_->setObjectName(QStringLiteral("parseProgress"));
  parseLayout->addWidget(parseProgress_);
  parseDetail_ = new QLabel(parseTracePanel_);
  parseDetail_->setWordWrap(true);
  parseDetail_->setObjectName(QStringLiteral("parseDetail"));
  parseLayout->addWidget(parseDetail_);
  parseTraceTree_ = new QTreeWidget(parseTracePanel_);
  parseTraceTree_->setObjectName(QStringLiteral("parseTraceTree"));
  parseTraceTree_->setHeaderLabels({tr("语法规则 / 事件"), tr("状态"), tr("源码位置")});
  parseTraceTree_->setAlternatingRowColors(true);
  parseTraceTree_->setUniformRowHeights(true);
  parseTraceTree_->setSelectionMode(QAbstractItemView::SingleSelection);
  parseTraceTree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  parseTraceTree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  parseTraceTree_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  parseLayout->addWidget(parseTraceTree_, 1);
  analysisTabs_->addTab(parseTracePanel_, tr("语法轨迹"));
  parseTimer_ = new QTimer(this);
  parseTimer_->setObjectName(QStringLiteral("parseTimer"));
  connect(parseTimer_, &QTimer::timeout, this, &MainWindow::nextParseEvent);
  connect(parseSpeed_, &QComboBox::currentIndexChanged, this, [this] {
    parseTimer_->setInterval(parseSpeed_->currentData().toInt());
  });
  connect(analysisTabs_, &QTabWidget::currentChanged, this, [this] {
    if (analysisTabs_->currentWidget() != parseTracePanel_) pauseParseTrace();
  });
  renderParseTrace();

  auto* runtimePanel = new QWidget(rightSplit);
  auto* runtimeLayout = new QVBoxLayout(runtimePanel);
  runtimeLayout->setContentsMargins(0, 0, 0, 0);
  runtimeLayout->setSpacing(8);

  auto* registerBar = new QFrame(runtimePanel);
  registerBar->setObjectName(QStringLiteral("registerBar"));
  auto* registerLayout = new QHBoxLayout(registerBar);
  registerLayout->setContentsMargins(14, 8, 14, 8);
  auto addRegister = [&](const QString& name, QLabel*& value) {
    auto* label = new QLabel(name, registerBar);
    label->setObjectName(QStringLiteral("registerName"));
    value = new QLabel(QStringLiteral("0000"), registerBar);
    value->setObjectName(QStringLiteral("registerValue"));
    registerLayout->addWidget(label);
    registerLayout->addWidget(value);
    registerLayout->addSpacing(12);
  };
  addRegister(QStringLiteral("PC"), pcValue_);
  addRegister(QStringLiteral("BP"), bpValue_);
  addRegister(QStringLiteral("SP"), spValue_);
  addRegister(QStringLiteral("STEP"), stepValue_);
  runtimeHint_ = new QLabel(tr("SP 指向下一空闲单元"), registerBar);
  runtimeHint_->setObjectName(QStringLiteral("muted"));
  registerLayout->addStretch();
  registerLayout->addWidget(runtimeHint_);
  runtimeLayout->addWidget(registerBar);

  auto* runtimeSplit = new QSplitter(Qt::Horizontal, runtimePanel);
  stackTable_ = makeTable({tr("地址"), tr("值"), tr("说明")}, runtimeSplit);
  framesTable_ = makeTable({tr("帧"), tr("过程"), tr("BP"), tr("SL"), tr("DL"), tr("RA")}, runtimeSplit);
  stackTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  framesTable_->horizontalHeader()->setStretchLastSection(true);
  framesTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  stackTable_->horizontalHeader()->setMinimumSectionSize(48);
  framesTable_->horizontalHeader()->setMinimumSectionSize(30);
  runtimeSplit->addWidget(makeCard(tr("运行栈"), stackTable_, runtimeSplit));
  runtimeSplit->addWidget(makeCard(tr("活动记录"), framesTable_, runtimeSplit));
  runtimeSplit->setStretchFactor(0, 3);
  runtimeSplit->setStretchFactor(1, 2);
  runtimeLayout->addWidget(runtimeSplit, 1);

  rightSplit->addWidget(analysisTabs_);
  rightSplit->addWidget(runtimePanel);
  rightSplit->setStretchFactor(0, 3);
  rightSplit->setStretchFactor(1, 2);

  mainSplit->addWidget(editorPane);
  mainSplit->addWidget(rightSplit);
  mainSplit->setStretchFactor(0, 5);
  mainSplit->setStretchFactor(1, 7);
  mainSplit->setSizes({560, 820});
  rightSplit->setSizes({320, 320});
  rootLayout->addWidget(mainSplit, 1);

  bottomTabs_ = new QTabWidget(root);
  bottomTabs_->setDocumentMode(true);
  bottomTabs_->setMaximumHeight(180);
  consoleEdit_ = new CodeEditor(bottomTabs_);
  consoleEdit_->setReadOnly(true);
  consoleEdit_->setObjectName(QStringLiteral("console"));
  consoleEdit_->setFont(mono);
  consoleEdit_->setLineNumberFont(mono);
  consoleEdit_->setPlaceholderText(tr("程序输出将显示在这里"));
  bottomTabs_->addTab(consoleEdit_, tr("控制台"));

  diagnosticsTable_ = makeTable({tr("级别"), tr("阶段"), tr("代码"), tr("位置"), tr("消息")}, bottomTabs_);
  diagnosticsTable_->setColumnWidth(0, 70);
  diagnosticsTable_->setColumnWidth(1, 80);
  diagnosticsTable_->setColumnWidth(2, 70);
  diagnosticsTable_->setColumnWidth(3, 120);
  bottomTabs_->addTab(diagnosticsTable_, tr("诊断  0"));

  auto* inputPanel = new QWidget(bottomTabs_);
  auto* inputLayout = new QHBoxLayout(inputPanel);
  inputLayout->setContentsMargins(16, 18, 16, 18);
  auto* inputLabel = new QLabel(tr("程序等待输入整数"), inputPanel);
  inputEdit_ = new QLineEdit(inputPanel);
  inputEdit_->setPlaceholderText(tr("例如：42"));
  inputButton_ = new QPushButton(tr("提交输入"), inputPanel);
  inputButton_->setObjectName(QStringLiteral("primaryButton"));
  inputLayout->addWidget(inputLabel);
  inputLayout->addWidget(inputEdit_, 1);
  inputLayout->addWidget(inputButton_);
  bottomTabs_->addTab(inputPanel, tr("程序输入"));

  watchTable_ = makeTable(
      {tr("名称"), tr("种类"), tr("层级"), tr("绝对地址"), tr("当前值")},
      bottomTabs_);
  watchTable_->setColumnWidth(0, 150);
  watchTable_->setColumnWidth(1, 90);
  watchTable_->setColumnWidth(2, 70);
  watchTable_->setColumnWidth(3, 100);
  bottomTabs_->addTab(watchTable_, tr("变量监视"));
  rootLayout->addWidget(bottomTabs_);

  setCentralWidget(root);
  runTimer_ = new QTimer(this);
  runTimer_->setInterval(35);
  statusBar()->setSizeGripEnabled(false);
  cursorPositionLabel_ = new QLabel(tr("行 1，列 1"), this);
  cursorPositionLabel_->setObjectName(QStringLiteral("muted"));
  statusBar()->addPermanentWidget(cursorPositionLabel_);
}

void MainWindow::setupActions() {
  newAction_ = new QAction(tr("新建"), this);
  openAction_ = new QAction(tr("打开"), this);
  saveAction_ = new QAction(tr("保存"), this);
  saveAsAction_ = new QAction(tr("另存为"), this);
  compileAction_ = new QAction(tr("编译"), this);
  runAction_ = new QAction(tr("运行"), this);
  pauseAction_ = new QAction(tr("暂停"), this);
  stepAction_ = new QAction(tr("单步"), this);
  resetAction_ = new QAction(tr("重置"), this);
  stopAction_ = new QAction(tr("停止"), this);
  boundsCheckAction_ = new QAction(tr("数组越界检查"), this);
  exportEvidenceAction_ = new QAction(tr("导出教学证据…"), this);
  aboutAction_ = new QAction(tr("关于 PL/0 可视化实验室"), this);
  boundsCheckAction_->setCheckable(true);
  boundsCheckAction_->setChecked(true);

  newAction_->setShortcut(QKeySequence::New);
  openAction_->setShortcut(QKeySequence::Open);
  saveAction_->setShortcut(QKeySequence::Save);
  saveAsAction_->setShortcut(QKeySequence::SaveAs);
  compileAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+B")));
  runAction_->setShortcuts({QKeySequence(QStringLiteral("F5")),
                            QKeySequence(QStringLiteral("Ctrl+R"))});
  pauseAction_->setShortcut(QKeySequence(QStringLiteral("F6")));
  stepAction_->setShortcut(QKeySequence(QStringLiteral("F10")));
  resetAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+F5")));
  exportEvidenceAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+E")));

  compileAction_->setStatusTip(tr("编译当前源码并刷新 Token、符号表和 P-Code（Ctrl+B）"));
  runAction_->setStatusTip(tr("从当前状态连续运行（F5；功能键被占用时使用 Ctrl+R）"));
  pauseAction_->setStatusTip(tr("暂停连续运行（F6）"));
  stepAction_->setStatusTip(tr("严格执行一条 P-Code 指令（F10）"));
  resetAction_->setStatusTip(tr("恢复到当前程序的初始运行状态（Ctrl+F5）"));
  exportEvidenceAction_->setStatusTip(tr("导出源码、编译数据与运行状态的 Markdown 报告"));

  auto* fileMenu = menuBar()->addMenu(tr("文件"));
  fileMenu->addActions({newAction_, openAction_, saveAction_, saveAsAction_});
  fileMenu->addSeparator();
  fileMenu->addAction(exportEvidenceAction_);
  auto* debugMenu = menuBar()->addMenu(tr("编译与调试"));
  debugMenu->addActions({compileAction_, runAction_, pauseAction_, stepAction_, resetAction_, stopAction_});
  auto* optionMenu = menuBar()->addMenu(tr("选项"));
  optionMenu->addAction(boundsCheckAction_);
  auto* helpMenu = menuBar()->addMenu(tr("帮助"));
  helpMenu->addAction(aboutAction_);

  auto* bar = addToolBar(tr("主工具栏"));
  bar->setObjectName(QStringLiteral("mainToolbar"));
  bar->setMovable(false);
  bar->setToolButtonStyle(Qt::ToolButtonTextOnly);
  bar->addActions({newAction_, openAction_, saveAction_});
  bar->addSeparator();
  auto* exampleText = new QLabel(tr("示例"), bar);
  exampleText->setObjectName(QStringLiteral("toolbarLabel"));
  bar->addWidget(exampleText);
  exampleCombo_ = new QComboBox(bar);
  exampleCombo_->addItems({tr("表达式与优先级"), tr("循环与复合赋值"), tr("过程调用与非局部变量")});
  exampleCombo_->setMinimumWidth(190);
  bar->addWidget(exampleCombo_);
  bar->addSeparator();
  bar->addAction(compileAction_);
  bar->addAction(runAction_);
  bar->addAction(pauseAction_);
  bar->addAction(stepAction_);
  bar->addAction(resetAction_);
  bar->addAction(stopAction_);
  bar->addSeparator();
  bar->addAction(boundsCheckAction_);
}

void MainWindow::setupConnections() {
  connect(newAction_, &QAction::triggered, this, &MainWindow::newFile);
  connect(openAction_, &QAction::triggered, this, &MainWindow::openFile);
  connect(saveAction_, &QAction::triggered, this, &MainWindow::saveFile);
  connect(saveAsAction_, &QAction::triggered, this, &MainWindow::saveFileAs);
  connect(compileAction_, &QAction::triggered, this, &MainWindow::compileSource);
  connect(runAction_, &QAction::triggered, this, &MainWindow::runProgram);
  connect(pauseAction_, &QAction::triggered, this, &MainWindow::pauseProgram);
  connect(stepAction_, &QAction::triggered, this, &MainWindow::stepProgram);
  connect(resetAction_, &QAction::triggered, this, &MainWindow::resetProgram);
  connect(stopAction_, &QAction::triggered, this, &MainWindow::stopProgram);
  connect(exportEvidenceAction_, &QAction::triggered, this, &MainWindow::exportEvidence);
  connect(aboutAction_, &QAction::triggered, this, &MainWindow::showAbout);
  connect(languageModeCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
          this, &MainWindow::compilerOptionsChanged);
  connect(boundsCheckAction_, &QAction::toggled,
          this, &MainWindow::compilerOptionsChanged);
  connect(sourceEdit_, &QPlainTextEdit::textChanged, this, &MainWindow::sourceChanged);
  connect(sourceEdit_, &QPlainTextEdit::cursorPositionChanged, this, &MainWindow::updateCursorPosition);
  connect(exampleCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::loadExample);
  connect(runTimer_, &QTimer::timeout, this, &MainWindow::runTick);
  connect(inputButton_, &QPushButton::clicked, this, &MainWindow::submitInput);
  connect(inputEdit_, &QLineEdit::returnPressed, this, &MainWindow::submitInput);
  connect(tokensTable_, &QTableWidget::cellClicked, this, &MainWindow::jumpToToken);
  connect(symbolsTable_, &QTableWidget::cellClicked, this, &MainWindow::jumpToSymbol);
  connect(pcodeTable_, &QTableWidget::cellClicked, this, &MainWindow::jumpToPCode);
  connect(pcodeTable_, &QTableWidget::cellDoubleClicked, this, &MainWindow::toggleBreakpoint);
  connect(diagnosticsTable_, &QTableWidget::cellClicked, this, &MainWindow::jumpToDiagnostic);
  connect(parseTraceTree_, &QTreeWidget::itemClicked, this, &MainWindow::jumpToParseTrace);
}

void MainWindow::closeEvent(QCloseEvent* event) {
  runTimer_->stop();
  if (promptToSave()) event->accept();
  else event->ignore();
}

void MainWindow::applyTheme() {
  setStyleSheet(QStringLiteral(R"(
    QMainWindow { background: #F4F7FB; color: #172033; }
    QMenuBar { background: #FFFFFF; border-bottom: 1px solid #E5EAF2; padding: 3px; }
    QMenuBar::item { padding: 6px 10px; border-radius: 5px; }
    QMenuBar::item:selected { background: #EEF4FF; color: #155EEF; }
    QToolBar#mainToolbar { background: #FFFFFF; border: none; border-bottom: 1px solid #E5EAF2; spacing: 5px; padding: 7px 12px; }
    QToolButton { color: #344054; border: 1px solid transparent; border-radius: 6px; padding: 7px 11px; font-weight: 600; }
    QToolButton:hover { background: #EEF4FF; color: #155EEF; }
    QToolButton:pressed { background: #DCE9FF; }
    QToolButton:disabled { color: #B3BDCC; }
    QFrame#intro { background: #123B6D; border-radius: 10px; }
    QLabel#appTitle { color: white; font-size: 20px; font-weight: 700; }
    QLabel#subtitle { color: #C9DCF5; }
    QComboBox#languageModeCombo { color: #EAF2FF; background: #214E82; border: 1px solid #3A6799; border-radius: 8px; padding: 5px 10px; }
    QComboBox#languageModeCombo::drop-down { border: none; width: 24px; }
    QComboBox#languageModeCombo QAbstractItemView { color: #344054; background: #FFFFFF; selection-background-color: #E6EFFF; selection-color: #123B6D; }
    QLabel#stateBadge { color: #123B6D; background: #FFFFFF; border-radius: 12px; padding: 5px 12px; font-weight: 700; }
    QLabel#sectionTitle, QLabel#cardTitle { color: #1D2939; font-size: 14px; font-weight: 700; }
    QLabel#muted { color: #667085; }
    QLabel#toolbarLabel { color: #667085; padding-left: 6px; }
    QFrame#card, QFrame#registerBar { background: #FFFFFF; border: 1px solid #E3E9F2; border-radius: 8px; }
    QLabel#registerName { color: #667085; font-size: 11px; font-weight: 700; }
    QLabel#registerValue { color: #155EEF; font-family: Consolas; font-size: 16px; font-weight: 700; }
    QPlainTextEdit, QLineEdit { background: #FFFFFF; color: #182230; border: 1px solid #D8E0EB; border-radius: 7px; selection-background-color: #BBD3FF; padding: 7px; }
    QPlainTextEdit#sourceEditor { border: 1px solid #C8D2E1; }
    QPlainTextEdit#console { background: #111827; color: #E5E7EB; border-color: #111827; }
    QPlainTextEdit:focus, QLineEdit:focus { border: 1px solid #4C8DFF; }
    QTabWidget::pane { background: #FFFFFF; border: 1px solid #E3E9F2; border-radius: 7px; top: -1px; }
    QTabBar::tab { color: #667085; background: transparent; padding: 9px 15px; border-bottom: 2px solid transparent; }
    QTabBar::tab:selected { color: #155EEF; border-bottom: 2px solid #155EEF; font-weight: 700; }
    QTableWidget, QTreeWidget { background: #FFFFFF; alternate-background-color: #F8FAFC; color: #344054; border: none; selection-background-color: #E6EFFF; selection-color: #123B6D; }
    QHeaderView::section { background: #F1F5F9; color: #475467; border: none; border-bottom: 1px solid #DDE4EE; padding: 8px; font-weight: 700; }
    QComboBox { background: #FFFFFF; border: 1px solid #D8E0EB; border-radius: 6px; padding: 6px 10px; color: #344054; }
    QPushButton { border: 1px solid #D0D8E5; border-radius: 6px; padding: 8px 15px; font-weight: 600; }
    QPushButton#primaryButton { color: white; background: #155EEF; border-color: #155EEF; }
    QPushButton#primaryButton:hover { background: #0B4EDB; }
    QStatusBar { background: #FFFFFF; color: #667085; border-top: 1px solid #E5EAF2; }
    QSplitter::handle { background: #E9EEF5; width: 4px; height: 4px; }
  )"));
}

void MainWindow::newFile() {
  if (!promptToSave()) return;
  loadingSource_ = true;
  sourceEdit_->clear();
  loadingSource_ = false;
  currentFilePath_.clear();
  documentDirty_ = false;
  lastResult_.reset();
  vm_.reset();
  breakpoints_.clear();
  skipBreakpointOnce_ = -1;
  pausedAtBreakpoint_ = false;
  tokensTable_->setRowCount(0);
  symbolsTable_->setRowCount(0);
  pcodeTable_->setRowCount(0);
  clearParseTrace();
  diagnosticsTable_->setRowCount(0);
  stackTable_->setRowCount(0);
  framesTable_->setRowCount(0);
  watchTable_->setRowCount(0);
  consoleEdit_->clear();
  setState(AppState::Idle, tr("新建源文件"));
  updateWindowTitle();
}

void MainWindow::openFile() {
  if (!promptToSave()) return;
  const QString path = QFileDialog::getOpenFileName(this, tr("打开 PL/0 程序"), {}, tr("PL/0 程序 (*.pl0 *.txt);;所有文件 (*)"));
  if (path.isEmpty()) return;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QMessageBox::critical(this, tr("无法打开"), tr("无法读取所选文件。"));
    return;
  }
  loadingSource_ = true;
  sourceEdit_->setPlainText(QString::fromUtf8(file.readAll()));
  loadingSource_ = false;
  currentFilePath_ = path;
  documentDirty_ = false;
  lastResult_.reset();
  vm_.reset();
  breakpoints_.clear();
  skipBreakpointOnce_ = -1;
  pausedAtBreakpoint_ = false;
  tokensTable_->setRowCount(0);
  symbolsTable_->setRowCount(0);
  pcodeTable_->setRowCount(0);
  clearParseTrace();
  diagnosticsTable_->setRowCount(0);
  stackTable_->setRowCount(0);
  framesTable_->setRowCount(0);
  watchTable_->setRowCount(0);
  consoleEdit_->clear();
  sourceHint_->setText(QFileInfo(path).fileName());
  setState(AppState::Idle, tr("已打开 %1").arg(QFileInfo(path).fileName()));
  updateWindowTitle();
}

void MainWindow::saveFile() {
  if (currentFilePath_.isEmpty()) saveFileAs();
  else saveToPath(currentFilePath_);
}

void MainWindow::saveFileAs() {
  const QString path = QFileDialog::getSaveFileName(this, tr("保存 PL/0 程序"), currentFilePath_, tr("PL/0 程序 (*.pl0);;文本文件 (*.txt)"));
  if (!path.isEmpty()) saveToPath(path);
}

bool MainWindow::saveToPath(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QMessageBox::critical(this, tr("无法保存"), tr("无法写入所选文件。"));
    return false;
  }
  file.write(sourceEdit_->toPlainText().toUtf8());
  currentFilePath_ = path;
  documentDirty_ = false;
  sourceHint_->setText(QFileInfo(path).fileName());
  updateWindowTitle();
  statusBar()->showMessage(tr("已保存 %1").arg(QFileInfo(path).fileName()), 3000);
  return true;
}

bool MainWindow::promptToSave() {
  if (!documentDirty_) return true;
  const auto choice = QMessageBox::question(this, tr("保存更改"), tr("源代码尚未保存，是否先保存？"), QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
  if (choice == QMessageBox::Cancel) return false;
  if (choice == QMessageBox::Save) {
    saveFile();
    return !documentDirty_;
  }
  return true;
}

void MainWindow::compileSource() { compileInternal(); }

bool MainWindow::compileInternal() {
  runTimer_->stop();
  pauseParseTrace();
  breakpoints_.clear();
  skipBreakpointOnce_ = -1;
  pausedAtBreakpoint_ = false;
  setState(AppState::Compiling, tr("正在执行词法、语法分析与代码生成…"));
  pl0::CompilerOptions options;
  options.enable_bounds_check = boundsCheckAction_->isChecked();
  options.language_mode = languageModeCombo_->currentIndex() == 0
                              ? pl0::LanguageMode::Baseline
                              : pl0::LanguageMode::Extended;
  pl0::DiagnosticSink diagnostics;
  const std::string name = currentFilePath_.isEmpty() ? "<memory>" : currentFilePath_.toStdString();
  const QString sourceText = sourceEdit_->toPlainText();
  const QByteArray sourceUtf8 = sourceText.toUtf8();
  const std::string source(sourceUtf8.constData(), static_cast<std::size_t>(sourceUtf8.size()));
  auto result = pl0::compile_source_text(name, source, options, diagnostics);
  populateTokens(result.tokens);
  populateParseTrace(result.parse_trace);
  refreshDiagnostics(diagnostics, tr("编译"));

  if (diagnostics.has_errors()) {
    lastResult_ = std::move(result);
    symbolsTable_->setRowCount(0);
    pcodeTable_->setRowCount(0);
    vm_.reset();
    setState(AppState::CompileFailed, tr("编译失败：请检查诊断信息"));
    bottomTabs_->setCurrentWidget(diagnosticsTable_);
    return false;
  }

  populateSymbols(result.symbols);
  populatePCode(result.code);
  lastResult_ = std::move(result);
  prepareVm();
  setState(AppState::Compiled,
           tr("编译成功 · %1 个 Token · %2 个符号 · %3 条指令")
               .arg(tokensTable_->rowCount()).arg(symbolsTable_->rowCount()).arg(pcodeTable_->rowCount()));
  analysisTabs_->setCurrentWidget(pcodeTable_);
  return true;
}

void MainWindow::prepareVm() {
  runtimeDiagnostics_.clear();
  runtimeOptions_.enable_bounds_check = boundsCheckAction_->isChecked();
  runtimeOptions_.trace_vm = false;
  vm_ = std::make_unique<pl0::VirtualMachine>(runtimeDiagnostics_, runtimeOptions_);
  if (lastResult_) vm_->load_program(lastResult_->code);
  refreshRuntime();
}

void MainWindow::runProgram() {
  if (!lastResult_ || state_ == AppState::SourceModified || state_ == AppState::CompileFailed) {
    if (!compileInternal()) return;
  }
  if (!vm_) prepareVm();
  if (vm_->status() == pl0::VirtualMachine::Status::Finished || state_ == AppState::Stopped) {
    resetProgram();
  }
  if (pausedAtBreakpoint_) {
    skipBreakpointOnce_ = vm_->snapshot().pc;
    pausedAtBreakpoint_ = false;
  } else {
    breakpointHitThisRun_ = false;
  }
  setState(AppState::Running, tr("正在逐条执行 P-Code"));
  runTimer_->start();
}

void MainWindow::pauseProgram() {
  runTimer_->stop();
  if (vm_) setState(AppState::Paused, tr("已暂停，可继续运行或单步执行"));
}

void MainWindow::stepProgram() {
  if (!lastResult_ || state_ == AppState::SourceModified || state_ == AppState::CompileFailed) {
    if (!compileInternal()) return;
  }
  if (!vm_) prepareVm();
  runTimer_->stop();
  pausedAtBreakpoint_ = false;
  skipBreakpointOnce_ = -1;
  vm_->step();
  refreshRuntime();
  if (vm_->status() == pl0::VirtualMachine::Status::NeedInput) {
    setState(AppState::NeedInput, tr("程序正在等待整数输入"));
    bottomTabs_->setCurrentIndex(2);
    inputEdit_->setFocus();
  } else if (vm_->status() == pl0::VirtualMachine::Status::Finished) {
    setState(AppState::Finished, tr("程序执行完成"));
  } else if (vm_->status() == pl0::VirtualMachine::Status::RuntimeError) {
    setState(AppState::RuntimeError, tr("程序发生运行时错误"));
  } else {
    setState(AppState::Paused, tr("已执行一条 P-Code 指令"));
  }
}

void MainWindow::resetProgram() {
  runTimer_->stop();
  if (!lastResult_) return;
  pausedAtBreakpoint_ = false;
  skipBreakpointOnce_ = -1;
  prepareVm();
  setState(AppState::Compiled, tr("运行状态已重置"));
}

void MainWindow::stopProgram() {
  runTimer_->stop();
  setState(AppState::Stopped, tr("运行已停止；重置后可重新执行"));
}

void MainWindow::runTick() {
  if (!vm_) { runTimer_->stop(); return; }
  const int nextPc = vm_->snapshot().pc;
  if (breakpoints_.contains(nextPc) && skipBreakpointOnce_ != nextPc) {
    runTimer_->stop();
    pausedAtBreakpoint_ = true;
    breakpointHitThisRun_ = true;
    refreshRuntime();
    if (nextPc >= 0 && nextPc < pcodeTable_->rowCount()) {
      for (int column = 0; column < pcodeTable_->columnCount(); ++column) {
        if (auto* item = pcodeTable_->item(nextPc, column))
          item->setBackground(QColor(QStringLiteral("#FEE4E2")));
      }
      pcodeTable_->selectRow(nextPc);
      pcodeTable_->scrollToItem(pcodeTable_->item(nextPc, 0));
    }
    setState(AppState::Paused,
             tr("命中 P-Code 断点 %1；可单步或继续运行").arg(nextPc));
    return;
  }
  if (skipBreakpointOnce_ == nextPc) skipBreakpointOnce_ = -1;
  if (vm_->snapshot().step >= 100000) {
    runTimer_->stop();
    setState(AppState::RuntimeError, tr("已达到 100000 步安全上限"));
    return;
  }
  vm_->step();
  refreshRuntime();
  switch (vm_->status()) {
    case pl0::VirtualMachine::Status::NeedInput:
      runTimer_->stop();
      setState(AppState::NeedInput, tr("程序正在等待整数输入"));
      bottomTabs_->setCurrentIndex(2);
      inputEdit_->setFocus();
      break;
    case pl0::VirtualMachine::Status::Finished:
      runTimer_->stop();
      setState(AppState::Finished,
               !breakpoints_.empty() && !breakpointHitThisRun_
                   ? tr("程序执行完成；已设置的断点不在本次执行路径上")
                   : tr("程序执行完成"));
      bottomTabs_->setCurrentWidget(consoleEdit_);
      break;
    case pl0::VirtualMachine::Status::RuntimeError:
      runTimer_->stop();
      refreshDiagnostics(runtimeDiagnostics_, tr("运行时"));
      setState(AppState::RuntimeError, tr("程序发生运行时错误"));
      bottomTabs_->setCurrentWidget(diagnosticsTable_);
      break;
    default:
      break;
  }
}

void MainWindow::submitInput() {
  if (!vm_ || state_ != AppState::NeedInput) return;
  bool ok = false;
  const qlonglong value = inputEdit_->text().trimmed().toLongLong(&ok);
  if (!ok) {
    inputEdit_->setStyleSheet(QStringLiteral("border: 1px solid #DC2626;"));
    statusBar()->showMessage(tr("请输入有效整数"), 3000);
    return;
  }
  inputEdit_->setStyleSheet({});
  inputEdit_->clear();
  vm_->provide_input(value);
  setState(AppState::Paused, tr("输入已提交，可继续运行"));
  stepProgram();
}

void MainWindow::loadExample(int index) {
  if (documentDirty_ && !promptToSave()) {
    QSignalBlocker blocker(exampleCombo_);
    return;
  }
  if (exampleCombo_ != nullptr && exampleCombo_->currentIndex() != index) {
    QSignalBlocker blocker(exampleCombo_);
    exampleCombo_->setCurrentIndex(index);
  }
  loadingSource_ = true;
  if (index == 0) sourceEdit_->setPlainText(kExpressionExample);
  else if (index == 1) sourceEdit_->setPlainText(kLoopExample);
  else sourceEdit_->setPlainText(kProcedureExample);
  loadingSource_ = false;
  currentFilePath_.clear();
  documentDirty_ = false;
  lastResult_.reset();
  vm_.reset();
  breakpoints_.clear();
  skipBreakpointOnce_ = -1;
  pausedAtBreakpoint_ = false;
  sourceHint_->setText(tr("内存示例 · PL/0"));
  tokensTable_->setRowCount(0);
  symbolsTable_->setRowCount(0);
  pcodeTable_->setRowCount(0);
  clearParseTrace();
  stackTable_->setRowCount(0);
  framesTable_->setRowCount(0);
  watchTable_->setRowCount(0);
  consoleEdit_->clear();
  setState(AppState::Idle, tr("已载入示例，点击“编译”查看结果"));
  updateWindowTitle();
}

void MainWindow::sourceChanged() {
  if (loadingSource_) return;
  clearParseTrace();
  documentDirty_ = true;
  lastResult_.reset();
  vm_.reset();
  runTimer_->stop();
  watchTable_->setRowCount(0);
  setState(AppState::SourceModified, tr("源码已修改，需要重新编译"));
  updateWindowTitle();
}

void MainWindow::compilerOptionsChanged() {
  runTimer_->stop();
  lastResult_.reset();
  vm_.reset();
  breakpoints_.clear();
  skipBreakpointOnce_ = -1;
  pausedAtBreakpoint_ = false;
  tokensTable_->setRowCount(0);
  symbolsTable_->setRowCount(0);
  pcodeTable_->setRowCount(0);
  clearParseTrace();
  diagnosticsTable_->setRowCount(0);
  stackTable_->setRowCount(0);
  framesTable_->setRowCount(0);
  watchTable_->setRowCount(0);
  consoleEdit_->clear();
  pcValue_->setText(QStringLiteral("----"));
  bpValue_->setText(QStringLiteral("----"));
  spValue_->setText(QStringLiteral("----"));
  stepValue_->setText(QStringLiteral("----"));
  runtimeHint_->setText(tr("等待编译并启动虚拟机"));
  setState(AppState::Idle,
           tr("编译选项已更改（%1），请重新编译")
               .arg(languageModeCombo_->currentText()));
}

void MainWindow::populateTokens(const std::vector<pl0::Token>& tokens) {
  tokensTable_->setRowCount(static_cast<int>(tokens.size()));
  for (int row = 0; row < static_cast<int>(tokens.size()); ++row) {
    const auto& token = tokens[static_cast<std::size_t>(row)];
    QString value;
    if (token.number) value = QString::number(*token.number);
    else if (token.boolean) value = *token.boolean ? tr("true") : tr("false");
    const QString location = tr("%1:%2").arg(token.range.begin.line).arg(token.range.begin.column);
    const QStringList values = {QString::number(row), QString::fromStdString(token.lexeme),
                                QString::fromStdString(pl0::to_string(token.kind)), value, location};
    for (int column = 0; column < values.size(); ++column) {
      auto* item = new QTableWidgetItem(values[column]);
      item->setData(kBeginLineRole, static_cast<qulonglong>(token.range.begin.line));
      item->setData(kBeginColumnRole, static_cast<qulonglong>(token.range.begin.column));
      item->setData(kEndLineRole, static_cast<qulonglong>(token.range.end.line));
      item->setData(kEndColumnRole, static_cast<qulonglong>(token.range.end.column));
      tokensTable_->setItem(row, column, item);
    }
  }
}

void MainWindow::populateSymbols(const std::vector<pl0::Symbol>& symbols) {
  symbolsTable_->setRowCount(static_cast<int>(symbols.size()));
  for (int row = 0; row < static_cast<int>(symbols.size()); ++row) {
    const auto& symbol = symbols[static_cast<std::size_t>(row)];
    const QString declaration = symbol.declaration_range
                                    ? tr("%1:%2")
                                          .arg(symbol.declaration_range->begin.line)
                                          .arg(symbol.declaration_range->begin.column)
                                    : QStringLiteral("—");
    const QStringList values = {QString::fromStdString(symbol.name), symbolKindName(symbol.kind),
                                varTypeName(symbol.type), QString::number(symbol.level),
                                QString::number(symbol.address), QString::number(symbol.size), declaration};
    for (int column = 0; column < values.size(); ++column) {
      auto* item = new QTableWidgetItem(values[column]);
      if (symbol.declaration_range) {
        item->setData(kBeginLineRole, static_cast<qulonglong>(symbol.declaration_range->begin.line));
        item->setData(kBeginColumnRole, static_cast<qulonglong>(symbol.declaration_range->begin.column));
        item->setData(kEndLineRole, static_cast<qulonglong>(symbol.declaration_range->end.line));
        item->setData(kEndColumnRole, static_cast<qulonglong>(symbol.declaration_range->end.column));
      }
      symbolsTable_->setItem(row, column, item);
    }
  }
}

void MainWindow::populatePCode(const pl0::InstructionSequence& code) {
  pcodeTable_->setRowCount(static_cast<int>(code.size()));
  for (int row = 0; row < static_cast<int>(code.size()); ++row) {
    const auto& instruction = code[static_cast<std::size_t>(row)];
    QString argument = QString::number(instruction.argument);
    if (instruction.op == pl0::Op::OPR)
      argument += QStringLiteral("  ·  ") + QString::fromStdString(pl0::to_string(static_cast<pl0::Opr>(instruction.argument)));
    const QString source = instruction.source_range
                               ? tr("%1:%2")
                                     .arg(instruction.source_range->begin.line)
                                     .arg(instruction.source_range->begin.column)
                               : tr("内部生成");
    const QStringList values = {QStringLiteral("%1").arg(row, 4, 10, QLatin1Char('0')),
                                QString::fromStdString(pl0::to_string(instruction.op)).toUpper(),
                                QString::number(instruction.level), argument, source};
    for (int column = 0; column < values.size(); ++column) {
      auto* item = new QTableWidgetItem(values[column]);
      if (column < 3) item->setTextAlignment(Qt::AlignCenter);
      if (instruction.source_range) {
        item->setData(kBeginLineRole, static_cast<qulonglong>(instruction.source_range->begin.line));
        item->setData(kBeginColumnRole, static_cast<qulonglong>(instruction.source_range->begin.column));
        item->setData(kEndLineRole, static_cast<qulonglong>(instruction.source_range->end.line));
        item->setData(kEndColumnRole, static_cast<qulonglong>(instruction.source_range->end.column));
      } else if (column == 4) {
        item->setForeground(QColor(QStringLiteral("#98A2B3")));
      }
      pcodeTable_->setItem(row, column, item);
    }
  }
  refreshBreakpointMarkers();
}

void MainWindow::refreshBreakpointMarkers() {
  for (int row = 0; row < pcodeTable_->rowCount(); ++row) {
    auto* item = pcodeTable_->item(row, 0);
    if (!item) continue;
    const bool enabled = breakpoints_.contains(row);
    item->setText((enabled ? QStringLiteral("● ") : QStringLiteral("  ")) +
                  QStringLiteral("%1").arg(row, 4, 10, QLatin1Char('0')));
    item->setForeground(QColor(enabled ? QStringLiteral("#D92D20")
                                       : QStringLiteral("#344054")));
    QFont font = item->font();
    font.setBold(enabled);
    item->setFont(font);
    item->setToolTip(enabled ? tr("断点已启用；双击取消")
                             : tr("双击设置断点"));
  }
}

void MainWindow::toggleBreakpoint(int row, int) {
  if (row < 0 || row >= pcodeTable_->rowCount()) return;
  if (breakpoints_.contains(row)) {
    breakpoints_.erase(row);
    statusBar()->showMessage(tr("已取消 P-Code 断点：%1").arg(row), 3000);
  } else {
    breakpoints_.insert(row);
    statusBar()->showMessage(tr("已设置 P-Code 断点：%1").arg(row), 3000);
  }
  refreshBreakpointMarkers();
}

void MainWindow::populateParseTrace(const std::vector<pl0::ParseTraceEvent>& trace) {
  pauseParseTrace();
  parseEvents_ = trace;
  // Preserve the existing complete overview after compilation. Reset or Play
  // starts an event-by-event replay of this immutable compiler trace.
  parsePosition_ = parseEvents_.size();
  renderParseTrace();
}

void MainWindow::clearParseTrace() {
  pauseParseTrace();
  parseEvents_.clear();
  parsePosition_ = 0;
  renderParseTrace();
}

void MainWindow::previousParseEvent() {
  pauseParseTrace();
  if (parsePosition_ > 0) --parsePosition_;
  renderParseTrace();
}

void MainWindow::nextParseEvent() {
  if (parsePosition_ < parseEvents_.size()) ++parsePosition_;
  if (parsePosition_ == parseEvents_.size()) parseTimer_->stop();
  renderParseTrace();
}

void MainWindow::playParseTrace() {
  if (parseEvents_.empty()) return;
  if (parsePosition_ == parseEvents_.size()) parsePosition_ = 0;
  parseTimer_->start(parseSpeed_->currentData().toInt());
  renderParseTrace();
}

void MainWindow::pauseParseTrace() {
  if (parseTimer_) parseTimer_->stop();
  if (parseProgress_) {
    parsePlay_->setEnabled(!parseEvents_.empty());
    parsePause_->setEnabled(false);
  }
}

void MainWindow::resetParseTrace() {
  pauseParseTrace();
  parsePosition_ = 0;
  renderParseTrace();
}

void MainWindow::showFullParseTrace() {
  pauseParseTrace();
  parsePosition_ = parseEvents_.size();
  renderParseTrace();
}

void MainWindow::renderParseTrace() {
  parseTraceTree_->clear();
  const bool playing = parseTimer_ && parseTimer_->isActive();
  const bool available = !parseEvents_.empty();
  parsePrevious_->setEnabled(parsePosition_ > 0 && !playing);
  parseNext_->setEnabled(parsePosition_ < parseEvents_.size() && !playing);
  parsePlay_->setEnabled(available && !playing);
  parsePause_->setEnabled(playing);
  parseReset_->setEnabled(available);
  parseFull_->setEnabled(available);
  parseProgress_->setText(tr("分析事件 %1 / %2 · %3（编译轨迹回放）")
      .arg(static_cast<qulonglong>(parsePosition_))
      .arg(static_cast<qulonglong>(parseEvents_.size()))
      .arg(playing ? tr("播放中") : tr("已暂停")));
  parseDetail_->setText(available ? tr("点击重置后逐步分析，或点击播放自动演示。")
                                 : tr("请先编译源码，再查看分析过程。"));
  std::vector<QTreeWidgetItem*> stack;
  QTreeWidgetItem* current = nullptr;
  for (std::size_t index = 0; index < parsePosition_; ++index) {
    const auto& event = parseEvents_[index];
    const QString location = tr("%1:%2")
                                 .arg(event.range.begin.line)
                                 .arg(event.range.begin.column);
    if (event.kind == pl0::ParseTraceKind::Exit) {
      if (!stack.empty()) {
        current = stack.back();
        stack.back()->setText(1, tr("完成"));
        stack.pop_back();
      }
      continue;
    }

    QString label = event.rule.empty() ? QString::fromStdString(event.detail)
                                       : QString::fromStdString(event.rule);
    auto* item = new QTreeWidgetItem({label, parseTraceKindName(event.kind), location});
    current = item;
    item->setData(0, kBeginLineRole, static_cast<qulonglong>(event.range.begin.line));
    item->setData(0, kBeginColumnRole, static_cast<qulonglong>(event.range.begin.column));
    item->setData(0, kEndLineRole, static_cast<qulonglong>(event.range.end.line));
    item->setData(0, kEndColumnRole, static_cast<qulonglong>(event.range.end.column));
    if (event.kind == pl0::ParseTraceKind::Error) {
      item->setForeground(0, QColor(QStringLiteral("#DC2626")));
      item->setForeground(1, QColor(QStringLiteral("#DC2626")));
    } else if (event.kind == pl0::ParseTraceKind::Recover) {
      item->setForeground(0, QColor(QStringLiteral("#D97706")));
      item->setForeground(1, QColor(QStringLiteral("#D97706")));
    } else if (event.kind == pl0::ParseTraceKind::Consume) {
      item->setForeground(0, QColor(QStringLiteral("#667085")));
    }

    if (stack.empty()) parseTraceTree_->addTopLevelItem(item);
    else stack.back()->addChild(item);
    if (event.kind == pl0::ParseTraceKind::Enter) stack.push_back(item);
  }
  parseTraceTree_->expandToDepth(2);
  if (current) {
    for (auto* parent = current->parent(); parent; parent = parent->parent()) parent->setExpanded(true);
    parseTraceTree_->setCurrentItem(current);
    parseTraceTree_->doItemsLayout();
    parseTraceTree_->scrollToItem(current, QAbstractItemView::PositionAtCenter);
    const auto& event = parseEvents_[parsePosition_ - 1];
    parseDetail_->setText(tr("%1 · %2 · 源码 %3:%4 · 嵌套深度 %5")
        .arg(parseTraceKindName(event.kind))
        .arg(QString::fromStdString(event.rule.empty() ? event.detail : event.rule))
        .arg(event.range.begin.line).arg(event.range.begin.column).arg(event.depth));
    if (analysisTabs_->currentWidget() == parseTracePanel_)
      selectSourceRange(event.range.begin.line, event.range.begin.column,
                        event.range.end.line, event.range.end.column);
  }
}

void MainWindow::refreshRuntime() {
  if (!vm_) return;
  const auto snapshot = vm_->snapshot();
  pcValue_->setText(QStringLiteral("%1").arg(snapshot.pc, 4, 10, QLatin1Char('0')));
  bpValue_->setText(QStringLiteral("%1").arg(snapshot.bp, 4, 10, QLatin1Char('0')));
  spValue_->setText(QStringLiteral("%1").arg(snapshot.sp, 4, 10, QLatin1Char('0')));
  stepValue_->setText(QStringLiteral("%1").arg(snapshot.step, 4, 10, QLatin1Char('0')));
  runtimeHint_->setText(tr("%1 · SP 指向下一空闲单元").arg(vmStatusName(snapshot.status)));
  consoleEdit_->setPlainText(QString::fromStdString(snapshot.output));
  consoleEdit_->moveCursor(QTextCursor::End);

  for (int row = 0; row < pcodeTable_->rowCount(); ++row) {
    for (int column = 0; column < pcodeTable_->columnCount(); ++column) {
      if (auto* item = pcodeTable_->item(row, column)) item->setBackground(Qt::transparent);
    }
  }
  if (snapshot.current_instruction >= 0 && snapshot.current_instruction < pcodeTable_->rowCount()) {
    for (int column = 0; column < pcodeTable_->columnCount(); ++column)
      pcodeTable_->item(snapshot.current_instruction, column)->setBackground(QColor(QStringLiteral("#DCE9FF")));
    pcodeTable_->selectRow(snapshot.current_instruction);
    pcodeTable_->scrollToItem(pcodeTable_->item(snapshot.current_instruction, 0));
  }

  const int stackRows = std::max(static_cast<int>(snapshot.stack.size()), snapshot.sp + 1);
  stackTable_->setRowCount(stackRows);
  for (int visualRow = 0; visualRow < stackRows; ++visualRow) {
    const int address = stackRows - visualRow - 1;
    const bool hasValue = address < static_cast<int>(snapshot.stack.size());
    QString note;
    for (const auto& frame : snapshot.frames) {
      if (address == frame.base) note += note.isEmpty() ? QStringLiteral("SL") : QStringLiteral(" · SL");
      if (address == frame.base + 1) note += note.isEmpty() ? QStringLiteral("DL") : QStringLiteral(" · DL");
      if (address == frame.base + 2) note += note.isEmpty() ? QStringLiteral("RA") : QStringLiteral(" · RA");
    }
    if (address == snapshot.bp) note += note.isEmpty() ? QStringLiteral("← BP") : QStringLiteral("  ← BP");
    if (address == snapshot.sp) note += note.isEmpty() ? tr("← SP 下一空闲") : tr("  ← SP 下一空闲");
    const QStringList values = {QString::number(address), hasValue ? QString::number(snapshot.stack[static_cast<std::size_t>(address)]) : QStringLiteral("—"), note};
    const bool changed = std::find(snapshot.changed_addresses.begin(), snapshot.changed_addresses.end(), address) != snapshot.changed_addresses.end();
    for (int column = 0; column < values.size(); ++column) {
      auto* item = new QTableWidgetItem(values[column]);
      if (column < 2) item->setTextAlignment(Qt::AlignCenter);
      if (changed) {
        item->setBackground(QColor(QStringLiteral("#FFF1D6")));
        item->setForeground(QColor(QStringLiteral("#B54708")));
      }
      stackTable_->setItem(visualRow, column, item);
    }
  }

  framesTable_->setRowCount(static_cast<int>(snapshot.frames.size()));
  for (int row = 0; row < static_cast<int>(snapshot.frames.size()); ++row) {
    const auto& frame = snapshot.frames[static_cast<std::size_t>(row)];
    QString procedureName = frame.entry_address == 0
                                ? tr("主程序")
                                : tr("入口 @%1").arg(frame.entry_address);
    if (lastResult_) {
      for (const auto& symbol : lastResult_->symbols) {
        if (symbol.kind == pl0::SymbolKind::Procedure &&
            symbol.address == frame.entry_address) {
          procedureName = QString::fromStdString(symbol.name);
          break;
        }
      }
    }
    const QStringList values = {frame.current ? tr("当前帧") : tr("调用帧"), procedureName,
                                QString::number(frame.base),
                                QString::number(frame.static_link), QString::number(frame.dynamic_link),
                                QString::number(frame.return_address)};
    for (int column = 0; column < values.size(); ++column) {
      auto* item = new QTableWidgetItem(values[column]);
      item->setTextAlignment(Qt::AlignCenter);
      if (frame.current) item->setBackground(QColor(QStringLiteral("#E8F7EE")));
      framesTable_->setItem(row, column, item);
    }
  }
  refreshVariableWatch(snapshot);
}

void MainWindow::refreshVariableWatch(
    const pl0::VirtualMachine::Snapshot& snapshot) {
  if (!lastResult_) {
    watchTable_->setRowCount(0);
    return;
  }

  std::vector<const pl0::Symbol*> watchable;
  for (const auto& symbol : lastResult_->symbols) {
    if (symbol.kind != pl0::SymbolKind::Procedure) watchable.push_back(&symbol);
  }
  watchTable_->setRowCount(static_cast<int>(watchable.size()));

  const auto frameLevel = [this](const pl0::VirtualMachine::FrameSnapshot& frame) {
    // SymbolTable starts with an administrative level 0 scope; the program
    // block enters level 1 and executes in the VM root frame.
    if (frame.entry_address == 0) return 1;
    if (!lastResult_) return -1;
    for (const auto& candidate : lastResult_->symbols) {
      if (candidate.kind == pl0::SymbolKind::Procedure &&
          candidate.address == frame.entry_address) {
        return candidate.level + 1;
      }
    }
    return -1;
  };

  for (int row = 0; row < static_cast<int>(watchable.size()); ++row) {
    const auto& symbol = *watchable[static_cast<std::size_t>(row)];
    QString absoluteAddress = QStringLiteral("—");
    QString value = tr("未分配");
    bool changed = false;

    if (symbol.kind == pl0::SymbolKind::Constant) {
      value = QString::number(symbol.constant_value);
    } else {
      const pl0::VirtualMachine::FrameSnapshot* ownerFrame = nullptr;
      for (auto frame = snapshot.frames.rbegin(); frame != snapshot.frames.rend(); ++frame) {
        if (frameLevel(*frame) == symbol.level) {
          ownerFrame = &*frame;
          break;
        }
      }
      if (ownerFrame) {
        const int address = ownerFrame->base + symbol.address;
        absoluteAddress = QString::number(address);
        const int count = symbol.kind == pl0::SymbolKind::Array
                              ? static_cast<int>(symbol.size)
                              : 1;
        QStringList elements;
        const int displayed = std::min(count, 8);
        bool allocated = true;
        for (int offset = 0; offset < displayed; ++offset) {
          const int currentAddress = address + offset;
          if (currentAddress < 0 ||
              currentAddress >= static_cast<int>(snapshot.stack.size())) {
            allocated = false;
            break;
          }
          elements << QString::number(
              snapshot.stack[static_cast<std::size_t>(currentAddress)]);
          changed = changed ||
                    std::find(snapshot.changed_addresses.begin(),
                              snapshot.changed_addresses.end(),
                              currentAddress) != snapshot.changed_addresses.end();
        }
        if (allocated) {
          value = symbol.kind == pl0::SymbolKind::Array
                      ? QStringLiteral("[%1%2]")
                            .arg(elements.join(QStringLiteral(", ")),
                                 count > displayed ? QStringLiteral(", …") : QString())
                      : elements.value(0);
        }
      }
    }

    const QStringList values = {
        QString::fromStdString(symbol.name), symbolKindName(symbol.kind),
        QString::number(symbol.level), absoluteAddress, value};
    for (int column = 0; column < values.size(); ++column) {
      auto* item = new QTableWidgetItem(values[column]);
      if (column >= 2 && column <= 3) item->setTextAlignment(Qt::AlignCenter);
      if (changed) {
        item->setBackground(QColor(QStringLiteral("#FFF1D6")));
        item->setForeground(QColor(QStringLiteral("#B54708")));
      }
      watchTable_->setItem(row, column, item);
    }
  }
}

void MainWindow::refreshDiagnostics(const pl0::DiagnosticSink& diagnostics, const QString& phase) {
  diagnosticsTable_->setRowCount(static_cast<int>(diagnostics.diagnostics().size()));
  int row = 0;
  for (const auto& diagnostic : diagnostics.diagnostics()) {
    const QString location = tr("%1:%2").arg(diagnostic.range.begin.line).arg(diagnostic.range.begin.column);
    const QStringList values = {diagnosticLevelName(diagnostic.level), phase,
                                QStringLiteral("E%1").arg(static_cast<int>(diagnostic.code), 3, 10, QLatin1Char('0')),
                                location, QString::fromStdString(diagnostic.message)};
    for (int column = 0; column < values.size(); ++column) {
      auto* item = new QTableWidgetItem(values[column]);
      item->setData(kBeginLineRole, static_cast<qulonglong>(diagnostic.range.begin.line));
      item->setData(kBeginColumnRole, static_cast<qulonglong>(diagnostic.range.begin.column));
      item->setData(kEndLineRole, static_cast<qulonglong>(diagnostic.range.end.line));
      item->setData(kEndColumnRole, static_cast<qulonglong>(diagnostic.range.end.column));
      if (column == 0) item->setForeground(diagnosticColor(diagnostic.level));
      diagnosticsTable_->setItem(row, column, item);
    }
    ++row;
  }
  bottomTabs_->setTabText(1, tr("诊断  %1").arg(row));
}

void MainWindow::jumpToToken(int row, int) {
  const auto* item = tokensTable_->item(row, 0);
  if (!item) return;
  tokensTable_->selectRow(row);
  tokensTable_->scrollToItem(item, QAbstractItemView::PositionAtCenter);
  selectSourceRange(item->data(kBeginLineRole).toULongLong(), item->data(kBeginColumnRole).toULongLong(),
                    item->data(kEndLineRole).toULongLong(), item->data(kEndColumnRole).toULongLong());
}

void MainWindow::jumpToSymbol(int row, int) {
  const auto* item = symbolsTable_->item(row, 0);
  if (!item || !item->data(kBeginLineRole).isValid()) return;
  symbolsTable_->selectRow(row);
  symbolsTable_->scrollToItem(item, QAbstractItemView::PositionAtCenter);
  selectSourceRange(item->data(kBeginLineRole).toULongLong(),
                    item->data(kBeginColumnRole).toULongLong(),
                    item->data(kEndLineRole).toULongLong(),
                    item->data(kEndColumnRole).toULongLong());
}

void MainWindow::jumpToPCode(int row, int) {
  const auto* item = pcodeTable_->item(row, 0);
  if (!item || !item->data(kBeginLineRole).isValid()) return;
  pcodeTable_->selectRow(row);
  pcodeTable_->scrollToItem(item, QAbstractItemView::PositionAtCenter);
  selectSourceRange(item->data(kBeginLineRole).toULongLong(),
                    item->data(kBeginColumnRole).toULongLong(),
                    item->data(kEndLineRole).toULongLong(),
                    item->data(kEndColumnRole).toULongLong());
}

void MainWindow::jumpToDiagnostic(int row, int) {
  const auto* item = diagnosticsTable_->item(row, 0);
  if (!item) return;
  diagnosticsTable_->selectRow(row);
  diagnosticsTable_->scrollToItem(item, QAbstractItemView::PositionAtCenter);
  selectSourceRange(item->data(kBeginLineRole).toULongLong(), item->data(kBeginColumnRole).toULongLong(),
                    item->data(kEndLineRole).toULongLong(), item->data(kEndColumnRole).toULongLong());
}

void MainWindow::jumpToParseTrace(QTreeWidgetItem* item, int) {
  if (item == nullptr) return;
  selectSourceRange(item->data(0, kBeginLineRole).toULongLong(),
                    item->data(0, kBeginColumnRole).toULongLong(),
                    item->data(0, kEndLineRole).toULongLong(),
                    item->data(0, kEndColumnRole).toULongLong());
}

void MainWindow::updateCursorPosition() {
  const QTextCursor cursor = sourceEdit_->textCursor();
  cursorPositionLabel_->setText(
      tr("行 %1，列 %2").arg(cursor.blockNumber() + 1).arg(cursor.positionInBlock() + 1));
}

void MainWindow::exportEvidence() {
  if (!lastResult_ || state_ == AppState::SourceModified) compileInternal();
  if (!lastResult_) return;

  const QString baseName = currentFilePath_.isEmpty()
                               ? tr("PL0_教学证据")
                               : QFileInfo(currentFilePath_).completeBaseName() + tr("_教学证据");
  const QString path = QFileDialog::getSaveFileName(
      this, tr("导出教学证据"), baseName + QStringLiteral(".md"),
      tr("Markdown 文档 (*.md);;文本文件 (*.txt)"));
  if (path.isEmpty()) return;

  QString report;
  QTextStream out(&report);
  out << "# PL/0 编译与运行教学证据\n\n";
  out << "- 导出时间：" << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) << "\n";
  out << "- 源文件：" << (currentFilePath_.isEmpty() ? tr("内存示例") : currentFilePath_) << "\n";
  out << "- 平台状态：" << stateName(state_) << "\n";
  out << "- 语言模式：" << languageModeCombo_->currentText() << "\n";
  out << "- 编译统计：" << tokensTable_->rowCount() << " 个 Token，"
      << symbolsTable_->rowCount() << " 个符号，" << pcodeTable_->rowCount() << " 条 P-Code\n\n";
  if (!breakpoints_.empty()) {
    out << "- P-Code 断点：";
    bool first = true;
    for (const int address : breakpoints_) {
      if (!first) out << "、";
      out << address;
      first = false;
    }
    out << "\n\n";
  }

  out << "## 源代码\n\n```pl0\n" << sourceEdit_->toPlainText() << "\n```\n\n";
  out << "## 词法单元\n\n" << tableAsMarkdown(tokensTable_) << "\n";
  out << "## 符号表\n\n" << tableAsMarkdown(symbolsTable_) << "\n";
  out << "## P-Code\n\n" << tableAsMarkdown(pcodeTable_) << "\n";
  out << "## 语法分析调用轨迹\n\n```text\n";
  for (const auto& event : lastResult_->parse_trace) {
    out << QString(std::max(0, event.depth) * 2, QLatin1Char(' '))
        << parseTraceKindName(event.kind) << ' ';
    if (!event.rule.empty()) out << QString::fromStdString(event.rule);
    if (!event.detail.empty()) out << QString::fromStdString(event.detail);
    out << "  [" << event.range.begin.line << ':' << event.range.begin.column << "]\n";
  }
  out << "```\n\n";
  out << "## 诊断\n\n" << tableAsMarkdown(diagnosticsTable_) << "\n";

  if (vm_) {
    const auto snapshot = vm_->snapshot();
    out << "## 虚拟机快照\n\n";
    out << "- PC：" << snapshot.pc << "\n- BP：" << snapshot.bp << "\n- SP：" << snapshot.sp
        << "（下一空闲）\n- STEP：" << snapshot.step << "\n- 状态："
        << vmStatusName(snapshot.status) << "\n\n";
    out << "### 运行栈\n\n" << tableAsMarkdown(stackTable_) << "\n";
    out << "### 活动记录（SL / DL / RA）\n\n" << tableAsMarkdown(framesTable_) << "\n";
    out << "### 变量监视\n\n" << tableAsMarkdown(watchTable_) << "\n";
  }

  out << "## 程序输出\n\n```text\n" << consoleEdit_->toPlainText() << "\n```\n";

  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QMessageBox::critical(this, tr("导出失败"), tr("无法写入所选文件。"));
    return;
  }
  file.write(report.toUtf8());
  file.close();
  statusBar()->showMessage(tr("教学证据已导出：%1").arg(QFileInfo(path).fileName()), 5000);
  QMessageBox::information(this, tr("导出完成"),
                           tr("已生成可直接用于实验报告的 Markdown 教学证据。"));
}

void MainWindow::showAbout() {
  QMessageBox::about(
      this, tr("关于 PL/0 可视化实验室"),
      tr("<h2>PL/0 编译实验与可视化调试平台</h2>"
         "<p>燕山大学《编译原理》三级项目 · Qt 6 Release</p>"
         "<p>支持 Baseline 教材基线与 Extended 扩展双模式。"
         "同一编译核心驱动 Token、符号表、P-Code、语法轨迹、诊断与虚拟机状态，"
         "用于观察源程序如何编译并在栈式虚拟机中执行。</p>"
         "<p><b>调试提示</b><br>双击任意 P-Code 行可设置或取消断点。</p>"
         "<p><b>快捷键</b><br>Ctrl+B 编译　F5 / Ctrl+R 运行　F6 暂停　F10 单步　Ctrl+F5 重置<br>"
         "Ctrl+Shift+E 导出教学证据</p>"
         "<p>版本 1.1.0 · C++20 / Qt 6.12</p>"));
}

void MainWindow::setDemoSource(QString source) {
  runTimer_->stop();
  loadingSource_ = true;
  sourceEdit_->setPlainText(std::move(source));
  loadingSource_ = false;
  currentFilePath_.clear();
  documentDirty_ = false;
  lastResult_.reset();
  vm_.reset();
  breakpoints_.clear();
  skipBreakpointOnce_ = -1;
  pausedAtBreakpoint_ = false;
  tokensTable_->setRowCount(0);
  symbolsTable_->setRowCount(0);
  pcodeTable_->setRowCount(0);
  clearParseTrace();
  diagnosticsTable_->setRowCount(0);
  stackTable_->setRowCount(0);
  framesTable_->setRowCount(0);
  watchTable_->setRowCount(0);
  consoleEdit_->clear();
  sourceHint_->setText(tr("自动演示源码 · PL/0"));
  setState(AppState::Idle, tr("已载入自动演示源码"));
  updateWindowTitle();
}

void MainWindow::showTokens() {
  analysisTabs_->setCurrentWidget(tokensTable_);
}

void MainWindow::showSymbols() {
  analysisTabs_->setCurrentWidget(symbolsTable_);
}

void MainWindow::showPCode() {
  analysisTabs_->setCurrentWidget(pcodeTable_);
}

void MainWindow::showParseTrace() {
  analysisTabs_->setCurrentWidget(parseTracePanel_);
}

void MainWindow::showDiagnostics() {
  bottomTabs_->setCurrentWidget(diagnosticsTable_);
}

void MainWindow::showConsole() {
  bottomTabs_->setCurrentWidget(consoleEdit_);
}

void MainWindow::showVariableWatch() {
  bottomTabs_->setCurrentWidget(watchTable_);
}

void MainWindow::showArrayCodegenForDemo() {
  analysisTabs_->setCurrentWidget(pcodeTable_);
  if (auto* item = pcodeTable_->item(4, 0)) {
    pcodeTable_->scrollToItem(item, QAbstractItemView::PositionAtCenter);
  }
}

void MainWindow::showFileMenu() {
  if (menuBar()->actions().isEmpty()) return;
  if (auto* menu = menuBar()->actions().constFirst()->menu()) {
    menu->popup(menuBar()->mapToGlobal(QPoint(0, menuBar()->height())));
  }
}

void MainWindow::markSourceModifiedForDemo() {
  QTextCursor cursor(sourceEdit_->document());
  cursor.movePosition(QTextCursor::End);
  cursor.insertText(QStringLiteral("\n// 已修改：旧编译结果自动失效"));
  sourceEdit_->setTextCursor(cursor);
}

void MainWindow::selectSourceRange(std::size_t beginLine, std::size_t beginColumn,
                                   std::size_t endLine, std::size_t endColumn) {
  QTextBlock beginBlock = sourceEdit_->document()->findBlockByNumber(static_cast<int>(beginLine > 0 ? beginLine - 1 : 0));
  QTextBlock endBlock = sourceEdit_->document()->findBlockByNumber(static_cast<int>(endLine > 0 ? endLine - 1 : 0));
  if (!beginBlock.isValid()) return;
  QTextCursor cursor(sourceEdit_->document());
  cursor.setPosition(beginBlock.position() + static_cast<int>(beginColumn > 0 ? beginColumn - 1 : 0));
  const int endPosition = endBlock.isValid() ? endBlock.position() + static_cast<int>(endColumn > 0 ? endColumn - 1 : 0)
                                             : beginBlock.position() + static_cast<int>(beginColumn);
  cursor.setPosition(std::max(cursor.position() + 1, endPosition), QTextCursor::KeepAnchor);
  sourceEdit_->setTextCursor(cursor);
  sourceEdit_->setFocus();
}

void MainWindow::setState(AppState state, const QString& detail) {
  state_ = state;
  stateBadge_->setText(stateName(state));
  QString color = QStringLiteral("#123B6D");
  QString background = QStringLiteral("#FFFFFF");
  if (state == AppState::CompileFailed || state == AppState::RuntimeError) { color = QStringLiteral("#B42318"); background = QStringLiteral("#FEE4E2"); }
  else if (state == AppState::Running) { color = QStringLiteral("#067647"); background = QStringLiteral("#D1FADF"); }
  else if (state == AppState::Paused || state == AppState::NeedInput) { color = QStringLiteral("#B54708"); background = QStringLiteral("#FEF0C7"); }
  else if (state == AppState::Compiled || state == AppState::Finished) { color = QStringLiteral("#175CD3"); background = QStringLiteral("#EAF2FF"); }
  stateBadge_->setStyleSheet(tr("color:%1;background:%2;border-radius:12px;padding:5px 12px;font-weight:700;").arg(color, background));
  statusBar()->showMessage(detail.isEmpty() ? stateName(state) : detail);
  updateActionState();
}

void MainWindow::updateActionState() {
  const bool busy = state_ == AppState::Compiling || state_ == AppState::Running;
  const bool validProgram = lastResult_.has_value() && !lastResult_->code.empty() &&
                            state_ != AppState::SourceModified && state_ != AppState::CompileFailed;
  compileAction_->setEnabled(!busy);
  runAction_->setEnabled(validProgram && state_ != AppState::Running && state_ != AppState::NeedInput);
  pauseAction_->setEnabled(state_ == AppState::Running);
  stepAction_->setEnabled(validProgram && state_ != AppState::Running && state_ != AppState::NeedInput && state_ != AppState::Finished);
  resetAction_->setEnabled(validProgram);
  stopAction_->setEnabled(state_ == AppState::Running || state_ == AppState::Paused || state_ == AppState::NeedInput);
  exportEvidenceAction_->setEnabled(!busy);
  inputEdit_->setEnabled(state_ == AppState::NeedInput);
  inputButton_->setEnabled(state_ == AppState::NeedInput);
}

void MainWindow::updateWindowTitle() {
  const QString name = currentFilePath_.isEmpty() ? tr("未命名") : QFileInfo(currentFilePath_).fileName();
  setWindowTitle(tr("%1%2 — PL/0 可视化实验室").arg(name, documentDirty_ ? QStringLiteral(" *") : QString()));
}

QString MainWindow::stateName(AppState state) const {
  switch (state) {
    case AppState::Idle: return tr("就绪");
    case AppState::SourceModified: return tr("源码已修改");
    case AppState::Compiling: return tr("编译中");
    case AppState::CompileFailed: return tr("编译失败");
    case AppState::Compiled: return tr("编译成功");
    case AppState::Running: return tr("运行中");
    case AppState::Paused: return tr("已暂停");
    case AppState::NeedInput: return tr("等待输入");
    case AppState::Finished: return tr("运行完成");
    case AppState::RuntimeError: return tr("运行错误");
    case AppState::Stopped: return tr("已停止");
  }
  return tr("未知");
}

QString MainWindow::vmStatusName(pl0::VirtualMachine::Status status) const {
  switch (status) {
    case pl0::VirtualMachine::Status::Ready: return tr("就绪");
    case pl0::VirtualMachine::Status::Paused: return tr("已暂停");
    case pl0::VirtualMachine::Status::NeedInput: return tr("等待输入");
    case pl0::VirtualMachine::Status::Finished: return tr("运行完成");
    case pl0::VirtualMachine::Status::RuntimeError: return tr("运行错误");
  }
  return tr("未知");
}

QString MainWindow::symbolKindName(pl0::SymbolKind kind) const {
  switch (kind) {
    case pl0::SymbolKind::Constant: return tr("常量");
    case pl0::SymbolKind::Variable: return tr("变量");
    case pl0::SymbolKind::Procedure: return tr("过程");
    case pl0::SymbolKind::Parameter: return tr("参数");
    case pl0::SymbolKind::Array: return tr("数组");
  }
  return tr("未知");
}

QString MainWindow::varTypeName(pl0::VarType type) const {
  switch (type) {
    case pl0::VarType::Integer: return tr("整数");
    case pl0::VarType::Boolean: return tr("布尔");
  }
  return tr("未知");
}
