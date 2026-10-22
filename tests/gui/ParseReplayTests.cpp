#include "MainWindow.hpp"
#include "CodeEditor.hpp"
#include "AstWidget.hpp"
#include <QApplication>
#include <QEventLoop>
#include <QFontDatabase>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <QDialog>
#include <iostream>
#include <stdexcept>

void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}

QString treeState(QTreeWidget* tree) {
  QString result;
  for (QTreeWidgetItemIterator it(tree); *it; ++it)
    result += (*it)->text(0) + ':' + (*it)->text(1) + '\n';
  return result;
}

int main(int argc, char** argv) {
  QApplication app(argc, argv);
#ifdef Q_OS_WIN
  // The offscreen platform does not discover Windows fonts automatically.
  const QString fonts = qEnvironmentVariable("WINDIR") + QStringLiteral("/Fonts/");
  QFontDatabase::addApplicationFont(fonts + QStringLiteral("msyh.ttc"));
  QFontDatabase::addApplicationFont(fonts + QStringLiteral("consola.ttf"));
  app.setFont(QFont(QStringLiteral("Microsoft YaHei"), 10));
#endif
  try {
    MainWindow window;
    auto invoke = [&](const char* slot) {
      require(QMetaObject::invokeMethod(&window, slot, Qt::DirectConnection), slot);
    };
    auto* tree = window.findChild<QTreeWidget*>("parseTraceTree");
    auto* progress = window.findChild<QLabel*>("parseProgress");
    auto* next = window.findChild<QPushButton*>("parseNext");
    auto* timer = window.findChild<QTimer*>("parseTimer");
    require(tree && progress && next && timer, "replay controls exist");
    require(!next->isEnabled(), "empty trace cannot advance");
    invoke("compileSource");
    invoke("showParseTrace");
    const QString full = treeState(tree);
    require(!full.isEmpty(), "compile creates full overview");
    invoke("resetParseTrace");
    require(tree->topLevelItemCount() == 0, "reset hides future events");
    invoke("nextParseEvent");
    require(tree->topLevelItemCount() == 1, "first step enters program");
    require(tree->currentItem()->text(0) == "program", "current rule selected");
    require(tree->currentItem()->text(1) != QString::fromUtf8("完成"), "no premature completion");
    invoke("nextParseEvent");
    const QString second = treeState(tree);
    invoke("previousParseEvent");
    invoke("nextParseEvent");
    require(treeState(tree) == second, "backtracking restores exact tree state");
    int steps = 0;
    while (next->isEnabled() && ++steps < 10000) invoke("nextParseEvent");
    require(steps < 10000 && treeState(tree) == full, "all events reproduce full overview");
    invoke("nextParseEvent");
    require(treeState(tree) == full, "end boundary stable");
    invoke("playParseTrace");
    require(timer->isActive(), "play restarts completed trace");
    QEventLoop loop;
    QTimer::singleShot(700, &loop, &QEventLoop::quit);
    loop.exec();
    require(tree->topLevelItemCount() > 0, "automatic timer advances events");
    invoke("pauseParseTrace");
    require(!timer->isActive(), "pause stops timer");
    invoke("playParseTrace");
    invoke("showPCode");
    require(!timer->isActive(), "switching tabs pauses replay");
    invoke("showParseTrace");
    invoke("resetParseTrace");
    for (int i = 0; i < 20; ++i) invoke("nextParseEvent");
    if (argc > 1) {
      window.show();
      app.processEvents();
      require(window.grab().save(QString::fromLocal8Bit(argv[1])), "save UI preview");
    }
    auto* editor = window.findChild<CodeEditor*>();
    require(editor != nullptr, "source editor exists");
    require(editor->textCursor().hasSelection(), "replay highlights source");
    invoke("playParseTrace");
    editor->insertPlainText(" ");
    require(!timer->isActive() && !next->isEnabled(), "editing invalidates replay");
    require(tree->topLevelItemCount() == 0, "editing removes stale events");
    QMetaObject::invokeMethod(&window, "setDemoSource", Qt::DirectConnection,
                            Q_ARG(QString, QStringLiteral("var x begin x := 1 end.")));
    invoke("compileSource");
    require(!treeState(tree).isEmpty(), "failed compilation preserves analysis trace");
    invoke("resetParseTrace");
    bool errorSeen = false;
    while (next->isEnabled()) {
      invoke("nextParseEvent");
      if (tree->currentItem() && tree->currentItem()->text(1) == QString::fromUtf8("错误"))
        errorSeen = true;
    }
    require(errorSeen, "error event can be replayed");
    auto* ast = window.findChild<AstWidget*>();
    auto* graph = window.findChild<QGraphicsView*>("astView");
    require(ast && graph && graph->scene()->items().empty(), "failed compile has no misleading AST");
    QMetaObject::invokeMethod(&window, "setDemoSource", Qt::DirectConnection,
        Q_ARG(QString, QStringLiteral("var x; begin x := 2 + 3 * 4 end.")));
    invoke("compileSource");
    invoke("showAst");
    QGraphicsRectItem* plus = nullptr;
    QGraphicsRectItem* multiply = nullptr;
    std::vector<QGraphicsRectItem*> nodes;
    for (auto* item : graph->scene()->items()) {
      if (auto* rect = dynamic_cast<QGraphicsRectItem*>(item)) {
        nodes.push_back(rect);
        if (rect->data(0).toString() == QString::fromUtf8("二元运算 +")) plus = rect;
        if (rect->data(0).toString() == QString::fromUtf8("二元运算 *")) multiply = rect;
      }
    }
    require(plus && multiply, "AST contains actual plus and multiplication nodes");
    require(multiply->rect().top() > plus->rect().top(), "multiplication nested beneath addition");
    for (std::size_t i = 0; i < nodes.size(); ++i)
      for (std::size_t j = i + 1; j < nodes.size(); ++j)
        require(!nodes[i]->rect().intersects(nodes[j]->rect()), "AST nodes do not overlap");
    multiply->setSelected(true);
    require(editor->textCursor().selectedText() == QStringLiteral("3 * 4"), "AST selection locates expression source");
    const qreal originalScale = graph->transform().m11();
    graph->scale(1.2, 1.2);
    require(graph->transform().m11() > originalScale, "graph supports zoom transform");
    if (argc > 2) {
      window.show();
      app.processEvents();
      require(window.grab().save(QString::fromLocal8Bit(argv[2])), "save AST preview");
    }
    auto* largeButton = window.findChild<QPushButton*>("astLarge");
    require(largeButton != nullptr, "large AST control exists");
    bool largeOpened = false;
    bool largeSaved = argc <= 3;
    QTimer::singleShot(50, &window, [&] {
      if (auto* dialog = window.findChild<QDialog*>()) {
        largeOpened = dialog->isVisible();
        if (argc > 3) largeSaved = dialog->grab().save(QString::fromLocal8Bit(argv[3]));
        dialog->accept();
      }
    });
    largeButton->click();
    require(largeOpened && largeSaved, "large AST opens and closes");
    editor->insertPlainText("5");
    require(graph->scene()->items().empty(), "editing invalidates AST");
    for (const auto& source : {
        "var a[3], x; begin a[1] := 2; x := a[1]; read(x); writeln(x) end.",
        "var x; procedure p; begin x := x + 1 end; begin call p end.",
        "var x; begin x := 0; while x < 3 do x++; if odd x then write(x) else write(0); repeat x-- until x = 0 end."}) {
      QMetaObject::invokeMethod(&window, "setDemoSource", Qt::DirectConnection,
                               Q_ARG(QString, QString::fromUtf8(source)));
      invoke("compileSource");
      require(!graph->scene()->items().empty(), "extended constructs render AST");
    }
    std::cout << "Parse replay GUI regression checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
