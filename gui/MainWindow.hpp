#pragma once

#include <memory>
#include <optional>
#include <set>

#include <QMainWindow>
#include <QString>

#include "pl0/Driver.hpp"

QT_BEGIN_NAMESPACE
class QAction;
class QCloseEvent;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTabWidget;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;
class QStackedWidget;
QT_END_NAMESPACE

class CodeEditor;
class AstWidget;

class MainWindow final : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);

 protected:
  void closeEvent(QCloseEvent* event) override;

 private Q_SLOTS:
  void newFile();
  void openFile();
  void saveFile();
  void saveFileAs();
  void compileSource();
  void runProgram();
  void pauseProgram();
  void stepProgram();
  void resetProgram();
  void stopProgram();
  void submitInput();
  void loadExample(int index);
  void sourceChanged();
  void compilerOptionsChanged();
  void runTick();
  void jumpToToken(int row, int column);
  void jumpToSymbol(int row, int column);
  void jumpToPCode(int row, int column);
  void toggleBreakpoint(int row, int column);
  void jumpToDiagnostic(int row, int column);
  void jumpToParseTrace(QTreeWidgetItem* item, int column);
  void updateCursorPosition();
  void exportEvidence();
  void showAbout();
  void setDemoSource(QString source);
  void showTokens();
  void showSymbols();
  void showPCode();
  void showParseTrace();
  void showAst();
  void showSyntaxTree();
  void showGrammarTree();
  void previousParseEvent();
  void nextParseEvent();
  void playParseTrace();
  void pauseParseTrace();
  void resetParseTrace();
  void showFullParseTrace();
  void showDiagnostics();
  void showConsole();
  void showVariableWatch();
  void showArrayCodegenForDemo();
  void showFileMenu();
  void markSourceModifiedForDemo();

 private:
  enum class AppState {
    Idle,
    SourceModified,
    Compiling,
    CompileFailed,
    Compiled,
    Running,
    Paused,
    NeedInput,
    Finished,
    RuntimeError,
    Stopped,
  };

  void setupUi();
  void setupActions();
  void setupConnections();
  void applyTheme();
  void setState(AppState state, const QString& detail = {});
  void updateActionState();
  void updateWindowTitle();
  bool compileInternal();
  bool saveToPath(const QString& path);
  bool promptToSave();
  void prepareVm();
  void refreshRuntime();
  void refreshVariableWatch(const pl0::VirtualMachine::Snapshot& snapshot);
  void refreshDiagnostics(const pl0::DiagnosticSink& diagnostics,
                          const QString& phase);
  void populateTokens(const std::vector<pl0::Token>& tokens);
  void populateSymbols(const std::vector<pl0::Symbol>& symbols);
  void populatePCode(const pl0::InstructionSequence& code);
  void refreshBreakpointMarkers();
  void populateParseTrace(const std::vector<pl0::ParseTraceEvent>& trace);
  void clearParseTrace();
  void renderParseTrace();
  void selectSourceRange(std::size_t beginLine, std::size_t beginColumn,
                         std::size_t endLine, std::size_t endColumn);
  QString stateName(AppState state) const;
  QString vmStatusName(pl0::VirtualMachine::Status status) const;
  QString symbolKindName(pl0::SymbolKind kind) const;
  QString varTypeName(pl0::VarType type) const;

  QString currentFilePath_;
  bool documentDirty_ = false;
  bool loadingSource_ = false;
  AppState state_ = AppState::Idle;

  CodeEditor* sourceEdit_ = nullptr;
  QTableWidget* tokensTable_ = nullptr;
  QTableWidget* symbolsTable_ = nullptr;
  QTableWidget* pcodeTable_ = nullptr;
  QTreeWidget* parseTraceTree_ = nullptr;
  AstWidget* astWidget_ = nullptr;
  AstWidget* syntaxTreeWidget_ = nullptr;
  AstWidget* grammarTreeWidget_ = nullptr;
  QWidget* treePanel_ = nullptr;
  QStackedWidget* treePages_ = nullptr;
  QComboBox* treeMode_ = nullptr;
  QWidget* parseTracePanel_ = nullptr;
  QLabel* parseProgress_ = nullptr;
  QLabel* parseDetail_ = nullptr;
  QPushButton* parsePrevious_ = nullptr;
  QPushButton* parseNext_ = nullptr;
  QPushButton* parsePlay_ = nullptr;
  QPushButton* parsePause_ = nullptr;
  QPushButton* parseReset_ = nullptr;
  QPushButton* parseFull_ = nullptr;
  QComboBox* parseSpeed_ = nullptr;
  QTimer* parseTimer_ = nullptr;
  std::vector<pl0::ParseTraceEvent> parseEvents_;
  std::size_t parsePosition_ = 0;
  QTableWidget* diagnosticsTable_ = nullptr;
  QTableWidget* stackTable_ = nullptr;
  QTableWidget* framesTable_ = nullptr;
  QTableWidget* watchTable_ = nullptr;
  CodeEditor* consoleEdit_ = nullptr;
  QLineEdit* inputEdit_ = nullptr;
  QPushButton* inputButton_ = nullptr;
  QTabWidget* analysisTabs_ = nullptr;
  QTabWidget* bottomTabs_ = nullptr;
  QComboBox* exampleCombo_ = nullptr;
  QComboBox* languageModeCombo_ = nullptr;
  QLabel* stateBadge_ = nullptr;
  QLabel* pcValue_ = nullptr;
  QLabel* bpValue_ = nullptr;
  QLabel* spValue_ = nullptr;
  QLabel* stepValue_ = nullptr;
  QLabel* sourceHint_ = nullptr;
  QLabel* runtimeHint_ = nullptr;
  QLabel* cursorPositionLabel_ = nullptr;
  QTimer* runTimer_ = nullptr;

  QAction* newAction_ = nullptr;
  QAction* openAction_ = nullptr;
  QAction* saveAction_ = nullptr;
  QAction* saveAsAction_ = nullptr;
  QAction* compileAction_ = nullptr;
  QAction* runAction_ = nullptr;
  QAction* pauseAction_ = nullptr;
  QAction* stepAction_ = nullptr;
  QAction* resetAction_ = nullptr;
  QAction* stopAction_ = nullptr;
  QAction* boundsCheckAction_ = nullptr;
  QAction* exportEvidenceAction_ = nullptr;
  QAction* aboutAction_ = nullptr;

  std::optional<pl0::CompileResult> lastResult_;
  pl0::DiagnosticSink runtimeDiagnostics_;
  pl0::RunnerOptions runtimeOptions_;
  std::unique_ptr<pl0::VirtualMachine> vm_;
  std::set<int> breakpoints_;
  int skipBreakpointOnce_ = -1;
  bool pausedAtBreakpoint_ = false;
  bool breakpointHitThisRun_ = false;
};
