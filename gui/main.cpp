#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QKeyEvent>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPixmap>
#include <QStyle>
#include <QTimer>

#include "MainWindow.hpp"

namespace {

void drawFrame(QPainter& painter, const QRect& box) {
  const QColor red(QStringLiteral("#D92D20"));
  painter.setPen(QPen(red, 4));
  painter.setBrush(Qt::NoBrush);
  painter.drawRoundedRect(box, 8, 8);
}

void drawTag(QPainter& painter, const QString& label, const QPoint& labelTopLeft) {
  const QColor red(QStringLiteral("#D92D20"));
  QFont font = painter.font();
  font.setPointSize(11);
  font.setBold(true);
  painter.setFont(font);
  const QFontMetrics metrics(font);
  const QRect labelBox(labelTopLeft,
                       QSize(metrics.horizontalAdvance(label) + 22,
                             metrics.height() + 12));
  painter.setPen(QPen(red, 1));
  painter.setBrush(QColor(255, 255, 255, 244));
  painter.drawRoundedRect(labelBox, 5, 5);
  painter.setPen(QPen(red, 1));
  painter.drawText(labelBox, Qt::AlignCenter, label);
}

void drawTaggedBox(QPainter& painter, const QRect& box, const QString& label,
                   const QPoint& labelTopLeft) {
  drawFrame(painter, box);
  drawTag(painter, label, labelTopLeft);
}

void annotateScreenshot(QPixmap& image, const QStringList& arguments) {
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  if (arguments.contains(QStringLiteral("--overview-demo"))) {
    drawTaggedBox(painter, QRect(14, 196, 588, 480),
                  QStringLiteral("源码编辑器：行号 / 语法高亮 / 当前行"), QPoint(135, 610));
    drawTaggedBox(painter, QRect(252, 38, 195, 45),
                  QStringLiteral("内置教学示例"), QPoint(455, 112));
    drawTaggedBox(painter, QRect(1370, 872, 100, 25),
                  QStringLiteral("光标行列状态"), QPoint(1160, 825));
  } else if (arguments.contains(QStringLiteral("--tokens-demo"))) {
    drawTaggedBox(painter, QRect(606, 196, 860, 235),
                  QStringLiteral("词法分析：词素 / 种类 / 值 / 行列位置"), QPoint(720, 112));
  } else if (arguments.contains(QStringLiteral("--symbols-demo"))) {
    drawTaggedBox(painter, QRect(606, 196, 860, 235),
                  QStringLiteral("符号表：类型 / 层级 / 地址 / 声明位置"), QPoint(720, 112));
  } else if (arguments.contains(QStringLiteral("--pcode-demo"))) {
    drawTaggedBox(painter, QRect(606, 292, 860, 30),
                  QStringLiteral("① 选中 P-Code 指令"), QPoint(790, 112));
    drawTaggedBox(painter, QRect(42, 273, 550, 22),
                  QStringLiteral("② 对应源码同步高亮"), QPoint(150, 610));
  } else if (arguments.contains(QStringLiteral("--breakpoint-demo")) ||
             arguments.contains(QStringLiteral("--shortcut-breakpoint-demo"))) {
    drawTaggedBox(painter, QRect(606, 382, 860, 36),
                  QStringLiteral("双击设断点；F5 / Ctrl+R 运行至断点"), QPoint(720, 112));
    drawTaggedBox(painter, QRect(1375, 116, 90, 50),
                  QStringLiteral("命中后自动暂停"), QPoint(1010, 120));
  } else if (arguments.contains(QStringLiteral("--watch-demo"))) {
    drawTaggedBox(painter, QRect(14, 684, 1452, 180),
                  QStringLiteral("数组整体值 + 标量值 / 层级 / 绝对地址"), QPoint(540, 820));
  } else if ((arguments.contains(QStringLiteral("--step-demo")) ||
              arguments.contains(QStringLiteral("--deep-step-demo"))) &&
             !arguments.contains(QStringLiteral("--procedure-demo"))) {
    drawTaggedBox(painter, QRect(440, 36, 405, 48),
                  QStringLiteral("运行 / 暂停 / 单步 / 重置 / 停止"), QPoint(455, 112));
    drawFrame(painter, QRect(606, 435, 860, 40));
    drawFrame(painter, QRect(606, 478, 510, 180));
    drawTag(painter, QStringLiteral("PC/BP/SP/STEP；橙色 = 本步变化栈单元"),
            QPoint(780, 112));
  } else if (arguments.contains(QStringLiteral("--trace-demo"))) {
    drawTaggedBox(painter, QRect(606, 196, 860, 235),
                  QStringLiteral("递归下降规则进入 / 退出 / 消费轨迹"), QPoint(750, 112));
  } else if (arguments.contains(QStringLiteral("--procedure-demo"))) {
    drawTaggedBox(painter, QRect(1115, 475, 350, 145),
                  QStringLiteral("过程调用帧：BP / SL / DL / RA"), QPoint(1100, 630));
  } else if (arguments.contains(QStringLiteral("--baseline-demo"))) {
    drawTaggedBox(painter, QRect(1190, 124, 180, 34),
                  QStringLiteral("Baseline 教材基线模式"), QPoint(850, 112));
    drawTaggedBox(painter, QRect(14, 684, 1452, 180),
                  QStringLiteral("扩展语法被准确诊断"), QPoint(590, 820));
  } else if (arguments.contains(QStringLiteral("--extension-demo"))) {
    drawTaggedBox(painter, QRect(1190, 124, 180, 34),
                  QStringLiteral("Extended 扩展模式"), QPoint(910, 112));
    drawTaggedBox(painter, QRect(45, 329, 545, 39),
                  QStringLiteral("复合赋值 += 与自增 ++ 编译通过"), QPoint(120, 560));
  } else if (arguments.contains(QStringLiteral("--diagnostic-location-demo"))) {
    drawTaggedBox(painter, QRect(14, 684, 1452, 180),
                  QStringLiteral("① 诊断阶段 / 编码 / 位置 / 原因"), QPoint(575, 620));
    drawTaggedBox(painter, QRect(42, 260, 550, 18),
                  QStringLiteral("② 同步定位错误源码"), QPoint(155, 610));
  } else if (arguments.contains(QStringLiteral("--diagnostics-demo")) ||
             arguments.contains(QStringLiteral("--syntax-error-demo")) ||
             arguments.contains(QStringLiteral("--array-error-demo"))) {
    drawTaggedBox(painter, QRect(14, 684, 1452, 180),
                  QStringLiteral("运行时错误：阶段 / 代码 / 位置 / 原因"), QPoint(535, 820));
    drawTaggedBox(painter, QRect(1375, 116, 90, 50),
                  QStringLiteral("运行错误状态"), QPoint(1010, 120));
  } else if (arguments.contains(QStringLiteral("--input-demo"))) {
    drawTaggedBox(painter, QRect(14, 684, 1452, 180),
                  QStringLiteral("非阻塞整数输入与提交"), QPoint(600, 705));
    drawTaggedBox(painter, QRect(1375, 116, 90, 50),
                  QStringLiteral("等待输入状态"), QPoint(1010, 120));
  } else if (arguments.contains(QStringLiteral("--console-demo"))) {
    drawTaggedBox(painter, QRect(14, 684, 1452, 180),
                  QStringLiteral("WRITE / WRITELN 程序输出"), QPoint(600, 820));
    drawTaggedBox(painter, QRect(1375, 116, 90, 50),
                  QStringLiteral("运行完成状态"), QPoint(1010, 120));
  } else if (arguments.contains(QStringLiteral("--array-codegen-demo"))) {
    drawTaggedBox(painter, QRect(606, 288, 860, 64),
                  QStringLiteral("CHK 检查下标；IDX 计算元素地址"), QPoint(720, 112));
    drawTaggedBox(painter, QRect(850, 38, 132, 46),
                  QStringLiteral("边界检查：当前开启"), QPoint(1000, 48));
  } else if (arguments.contains(QStringLiteral("--file-menu-demo"))) {
    drawTaggedBox(painter, QRect(0, 28, 225, 185),
                  QStringLiteral("文件管理与 Markdown 证据导出"), QPoint(320, 112));
  } else if (arguments.contains(QStringLiteral("--modified-demo"))) {
    drawTaggedBox(painter, QRect(42, 343, 550, 28),
                  QStringLiteral("编辑后自动标记为待重新编译"), QPoint(150, 520));
    drawTaggedBox(painter, QRect(1375, 116, 90, 50),
                  QStringLiteral("旧结果失效，运行被禁用"), QPoint(875, 120));
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  QApplication::setApplicationDisplayName(QStringLiteral("PL/0 Studio"));
  QApplication::setApplicationVersion(QStringLiteral("1.1.0"));
  QApplication::setOrganizationName(QStringLiteral("PL0"));

  if (QApplication::style()->objectName().compare("fusion", Qt::CaseInsensitive) != 0) {
    QApplication::setStyle("fusion");
  }

  MainWindow window;
  window.show();

  // Headless-friendly visual smoke test used by CI and release QA:
  // pl0-gui --screenshot <absolute-or-relative-output.png>
  const QStringList arguments = app.arguments();
  const int screenshotIndex = arguments.indexOf(QStringLiteral("--screenshot"));
  if (screenshotIndex >= 0 && screenshotIndex + 1 < arguments.size()) {
    const QString outputPath = QDir::current().absoluteFilePath(arguments.at(screenshotIndex + 1));
    if (arguments.contains(QStringLiteral("--syntax-error-demo"))) {
      QMetaObject::invokeMethod(
          &window, "setDemoSource", Qt::DirectConnection,
          Q_ARG(QString, QStringLiteral(
              "var x;\n"
              "begin\n"
              "  x := (2 + 3;\n"
              "  writeln(x)\n"
              "end.\n")));
    }
    if (arguments.contains(QStringLiteral("--array-error-demo"))) {
      QMetaObject::invokeMethod(
          &window, "setDemoSource", Qt::DirectConnection,
          Q_ARG(QString, QStringLiteral(
              "var a[2], result;\n"
              "begin\n"
              "  a[0] := 10;\n"
              "  a[1] := 20;\n"
              "  a[3] := 99;\n"
              "  result := a[0] + a[1];\n"
              "  writeln(result)\n"
              "end.\n")));
    }
    if (arguments.contains(QStringLiteral("--input-demo"))) {
      QMetaObject::invokeMethod(
          &window, "setDemoSource", Qt::DirectConnection,
          Q_ARG(QString, QStringLiteral(
              "var x;\n"
              "begin\n"
              "  read(x);\n"
              "  writeln(x * 2)\n"
              "end.\n")));
    }
    if (arguments.contains(QStringLiteral("--array-demo"))) {
      QMetaObject::invokeMethod(
          &window, "setDemoSource", Qt::DirectConnection,
          Q_ARG(QString, QStringLiteral(
              "var a[3], i, sum;\n"
              "begin\n"
              "  a[0] := 10; a[1] := 20; a[2] := 30;\n"
              "  i := 0; sum := 0;\n"
              "  while i < 3 do\n"
              "  begin\n"
              "    sum += a[i];\n"
              "    i++\n"
              "  end;\n"
              "  writeln(sum)\n"
              "end.\n")));
    }
    if (arguments.contains(QStringLiteral("--procedure-demo"))) {
      QMetaObject::invokeMethod(&window, "loadExample", Qt::DirectConnection,
                                Q_ARG(int, 2));
    }
    if (arguments.contains(QStringLiteral("--extension-demo"))) {
      QMetaObject::invokeMethod(&window, "loadExample", Qt::DirectConnection,
                                Q_ARG(int, 1));
    }
    if (arguments.contains(QStringLiteral("--baseline-demo"))) {
      if (auto* mode = window.findChild<QComboBox*>(QStringLiteral("languageModeCombo"))) {
        mode->setCurrentIndex(0);
      }
    }
    if (arguments.contains(QStringLiteral("--compile-demo"))) {
      QMetaObject::invokeMethod(&window, "compileSource", Qt::QueuedConnection);
    }
    if (arguments.contains(QStringLiteral("--run-demo"))) {
      QTimer::singleShot(120, &window, [&window]() {
        QMetaObject::invokeMethod(&window, "runProgram", Qt::DirectConnection);
      });
    }
    if (arguments.contains(QStringLiteral("--step-demo")) ||
        arguments.contains(QStringLiteral("--deep-step-demo"))) {
      const int stepCount = arguments.contains(QStringLiteral("--deep-step-demo")) ? 16 : 8;
      QTimer::singleShot(120, &window, [&window, stepCount]() {
        for (int i = 0; i < stepCount; ++i) {
          QMetaObject::invokeMethod(&window, "stepProgram", Qt::DirectConnection);
        }
      });
    }
    if (arguments.contains(QStringLiteral("--breakpoint-demo"))) {
      QTimer::singleShot(120, &window, [&window]() {
        QMetaObject::invokeMethod(&window, "toggleBreakpoint", Qt::DirectConnection,
                                  Q_ARG(int, 4), Q_ARG(int, 0));
        QMetaObject::invokeMethod(&window, "runProgram", Qt::DirectConnection);
      });
    }
    if (arguments.contains(QStringLiteral("--shortcut-breakpoint-demo"))) {
      QTimer::singleShot(120, &window, [&window]() {
        QMetaObject::invokeMethod(&window, "toggleBreakpoint", Qt::DirectConnection,
                                  Q_ARG(int, 4), Q_ARG(int, 0));
        window.activateWindow();
        window.setFocus();
        QKeyEvent press(QEvent::KeyPress, Qt::Key_F5, Qt::NoModifier);
        QApplication::sendEvent(&window, &press);
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_F5, Qt::NoModifier);
        QApplication::sendEvent(&window, &release);
      });
    }
    if (arguments.contains(QStringLiteral("--trace-demo"))) {
      QTimer::singleShot(220, &window, [&window]() {
        QMetaObject::invokeMethod(&window, "showParseTrace", Qt::DirectConnection);
      });
    }
    if (arguments.contains(QStringLiteral("--tokens-demo"))) {
      QTimer::singleShot(220, &window, [&window]() {
        QMetaObject::invokeMethod(&window, "showTokens", Qt::DirectConnection);
        QMetaObject::invokeMethod(&window, "jumpToToken", Qt::DirectConnection,
                                  Q_ARG(int, 8), Q_ARG(int, 0));
      });
    }
    if (arguments.contains(QStringLiteral("--symbols-demo"))) {
      QTimer::singleShot(220, &window, [&window]() {
        QMetaObject::invokeMethod(&window, "showSymbols", Qt::DirectConnection);
        QMetaObject::invokeMethod(&window, "jumpToSymbol", Qt::DirectConnection,
                                  Q_ARG(int, 0), Q_ARG(int, 0));
      });
    }
    if (arguments.contains(QStringLiteral("--pcode-demo"))) {
      QTimer::singleShot(220, &window, [&window]() {
        QMetaObject::invokeMethod(&window, "showPCode", Qt::DirectConnection);
        QMetaObject::invokeMethod(&window, "jumpToPCode", Qt::DirectConnection,
                                  Q_ARG(int, 4), Q_ARG(int, 0));
      });
    }
    if (arguments.contains(QStringLiteral("--diagnostics-demo"))) {
      QTimer::singleShot(260, &window, [&window]() {
        QMetaObject::invokeMethod(&window, "showDiagnostics", Qt::DirectConnection);
      });
    }
    if (arguments.contains(QStringLiteral("--diagnostic-location-demo"))) {
      QTimer::singleShot(260, &window, [&window]() {
        QMetaObject::invokeMethod(&window, "showDiagnostics", Qt::DirectConnection);
        QMetaObject::invokeMethod(&window, "jumpToDiagnostic", Qt::DirectConnection,
                                  Q_ARG(int, 0), Q_ARG(int, 0));
      });
    }
    if (arguments.contains(QStringLiteral("--console-demo"))) {
      QTimer::singleShot(760, &window, [&window]() {
        QMetaObject::invokeMethod(&window, "showConsole", Qt::DirectConnection);
      });
    }
    if (arguments.contains(QStringLiteral("--watch-demo"))) {
      QTimer::singleShot(260, &window, [&window]() {
        QMetaObject::invokeMethod(&window, "showVariableWatch", Qt::DirectConnection);
      });
    }
    if (arguments.contains(QStringLiteral("--array-codegen-demo"))) {
      QTimer::singleShot(260, &window, [&window]() {
        QMetaObject::invokeMethod(&window, "showArrayCodegenForDemo", Qt::DirectConnection);
      });
    }
    if (arguments.contains(QStringLiteral("--file-menu-demo"))) {
      QTimer::singleShot(260, &window, [&window]() {
        QMetaObject::invokeMethod(&window, "showFileMenu", Qt::DirectConnection);
      });
    }
    if (arguments.contains(QStringLiteral("--modified-demo"))) {
      QTimer::singleShot(300, &window, [&window]() {
        QMetaObject::invokeMethod(&window, "markSourceModifiedForDemo", Qt::DirectConnection);
      });
    }
    QTimer::singleShot(1000, &window, [&window, outputPath, arguments, &app]() {
      QPixmap screenshot = window.grab();
      if (arguments.contains(QStringLiteral("--file-menu-demo")) &&
          !window.menuBar()->actions().isEmpty()) {
        if (auto* menu = window.menuBar()->actions().constFirst()->menu()) {
          QPainter compositor(&screenshot);
          compositor.drawPixmap(window.mapFromGlobal(menu->pos()), menu->grab());
        }
      }
      if (arguments.contains(QStringLiteral("--annotated"))) {
        annotateScreenshot(screenshot, arguments);
      }
      screenshot.save(outputPath);
      app.quit();
    });
  }
  return QApplication::exec();
}
