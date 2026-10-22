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
#include <QComboBox>
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
  QFontDatabase::addApplicationFont(fonts + QStringLiteral("times.ttf"));
  QFontDatabase::addApplicationFont(fonts + QStringLiteral("timesi.ttf"));
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
    QMetaObject::invokeMethod(&window, "setDemoSource", Qt::DirectConnection,
        Q_ARG(QString, QStringLiteral("var x; begin x := 2 + 3 end.")));
    invoke("compileSource");
    invoke("showSyntaxTree");
    auto* syntaxView = window.findChild<QGraphicsView*>("syntaxTreeView");
    require(syntaxView != nullptr, "textbook view exists");
    auto labels = [&] {
      QStringList result;
      for (auto* item : syntaxView->scene()->items())
        if (auto* rect = dynamic_cast<QGraphicsRectItem*>(item)) {
          require(rect->pen().style() == Qt::NoPen, "textbook nodes have no boxes");
          result.append(rect->data(0).toString());
        }
      return result;
    };
    const auto completeLabels = labels();
    require(completeLabels.contains(QString::fromUtf8("表达式")) &&
            completeLabels.contains("+") && completeLabels.contains(";") &&
            completeLabels.contains("."), "textbook tree preserves rules and punctuation");
    invoke("resetParseTrace");
    require(syntaxView->scene()->items().empty(), "textbook reset clears future tree");
    invoke("nextParseEvent");
    require(labels().size() == 1, "textbook first step shows root only");
    invoke("nextParseEvent");
    const auto secondLabels = labels();
    invoke("previousParseEvent");
    invoke("nextParseEvent");
    require(labels() == secondLabels, "textbook backward and forward are deterministic");
    invoke("playParseTrace");
    require(timer->isActive(), "textbook playback starts");
    invoke("pauseParseTrace");
    require(!timer->isActive(), "textbook playback pauses");
    invoke("showFullParseTrace");
    require(labels() == completeLabels, "textbook full overview restored");
    if (argc > 4) {
      invoke("resetParseTrace");
      for (int i = 0; i < 18; ++i) invoke("nextParseEvent");
      auto* syntaxPanel = syntaxView->parentWidget();
      auto* large = syntaxPanel->findChild<QPushButton*>("astLarge");
      bool saved = false;
      QTimer::singleShot(50, &window, [&] {
        if (auto* dialog = syntaxPanel->findChild<QDialog*>()) {
          saved = dialog->grab().save(QString::fromLocal8Bit(argv[4]));
          dialog->accept();
        }
      });
      large->click();
      require(saved, "save textbook preview");
    }
    QMetaObject::invokeMethod(&window, "setDemoSource", Qt::DirectConnection,
        Q_ARG(QString, QStringLiteral("var a,b,c,x; begin x := a + b * c end.")));
    invoke("compileSource");
    invoke("showGrammarTree");
    auto* grammar = window.findChild<AstWidget*>("grammarPanel");
    auto* formalView = window.findChild<QGraphicsView*>("grammarTreeView");
    require(grammar && formalView, "formal E/T/F tree exists");
    auto formalLabels = [&] {
      QStringList result;
      for (auto* item : formalView->scene()->items())
        if (auto* rect = dynamic_cast<QGraphicsRectItem*>(item)) result.append(rect->data(0).toString());
      return result;
    };
    const auto fullFormal = formalLabels();
    require(fullFormal.count("E") == 2 && fullFormal.count("T") == 3 && fullFormal.count("F") == 3,
            "a+b*c matches textbook left-recursive E/T/F structure");
    require(fullFormal.contains("+") && fullFormal.contains("*") && fullFormal.contains("a"),
            "formal leaves preserve terminals");
    grammar->grammarCommand(4);
    require(formalLabels() == QStringList{"E"}, "formal derivation starts with E");
    grammar->grammarCommand(1);
    require(formalLabels().size() == 4, "first production expands E to E plus T");
    grammar->grammarCommand(0);
    require(formalLabels() == QStringList{"E"}, "formal backward restores start symbol");
    grammar->grammarCommand(5);
    require(formalLabels() == fullFormal, "formal complete derivation restored");
    auto checkFormalLayout = [&] {
      std::vector<QRectF> boxes;
      for (auto* item : formalView->scene()->items())
        if (auto* rect = dynamic_cast<QGraphicsRectItem*>(item)) boxes.push_back(rect->sceneBoundingRect());
      for (std::size_t i = 0; i < boxes.size(); ++i)
        for (std::size_t j = i + 1; j < boxes.size(); ++j)
          require(!boxes[i].intersects(boxes[j]), "book-style node labels do not overlap");
    };
    checkFormalLayout();
    if (argc > 5) {
      bool saved = false;
      QTimer::singleShot(50, &window, [&] {
        if (auto* dialog = grammar->findChild<QDialog*>()) {
          saved = dialog->grab().save(QString::fromLocal8Bit(argv[5]));
          dialog->accept();
        }
      });
      grammar->findChild<QPushButton*>("astLarge")->click();
      require(saved, "save E/T/F tree preview");
    }
    QMetaObject::invokeMethod(&window, "setDemoSource", Qt::DirectConnection,
        Q_ARG(QString, QStringLiteral("var a,b,c,x; begin x := (a + b) * c end.")));
    invoke("compileSource");
    auto* choices = grammar->findChild<QComboBox*>("grammarExpression");
    for (int i = 0; i < choices->count(); ++i)
      if (choices->itemText(i).contains("( a + b ) * c")) choices->setCurrentIndex(i);
    grammar->grammarCommand(5);
    require(formalLabels().contains("(") && formalLabels().contains(")"), "formal tree retains parentheses");
    checkFormalLayout();
    QMetaObject::invokeMethod(&window, "setDemoSource", Qt::DirectConnection,
        Q_ARG(QString, QStringLiteral("var long_name,other_value,x; begin x := long_name + other_value * (long_name + 12345) - other_value / 2 end.")));
    invoke("compileSource");
    for (int i = 0; i < choices->count(); ++i)
      if (choices->itemText(i).contains("long_name + other_value *")) choices->setCurrentIndex(i);
    grammar->grammarCommand(5);
    checkFormalLayout();
    QMetaObject::invokeMethod(&window, "setDemoSource", Qt::DirectConnection,
        Q_ARG(QString, QStringLiteral("var x; begin x := 1 < 2 end.")));
    invoke("compileSource");
    require(formalView->scene()->items().empty(), "unsupported grammar is not fabricated");
    std::cout << "Parse replay GUI regression checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
