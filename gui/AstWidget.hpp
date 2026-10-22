#pragma once

#include <QWidget>
#include "pl0/AST.hpp"
#include "pl0/Parser.hpp"

class QGraphicsScene;
class QGraphicsView;
class QLabel;
class QShowEvent;
class QResizeEvent;
class QComboBox;
class QTimer;

class AstWidget final : public QWidget {
  Q_OBJECT
 public:
  explicit AstWidget(QWidget* parent = nullptr, bool textbook = false, bool grammar = false);
  void setProgram(const pl0::Program* program);
  void setParseTrace(const std::vector<pl0::ParseTraceEvent>& events, std::size_t position);
  void setGrammarTrace(const std::vector<pl0::ParseTraceEvent>& events);
  void grammarCommand(int command);

 protected:
  void showEvent(QShowEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

 Q_SIGNALS:
  void sourceActivated(qulonglong beginLine, qulonglong beginColumn,
                       qulonglong endLine, qulonglong endColumn);
  void replayRequested(int command);

 private:
  QGraphicsScene* scene_;
  QGraphicsView* view_;
  QLabel* hint_;
  bool fitPending_ = true;
  bool textbook_ = false;
  bool grammar_ = false;
  QComboBox* expressionChoice_ = nullptr;
  QTimer* grammarTimer_ = nullptr;
  std::vector<std::vector<pl0::ParseTraceEvent>> expressions_;
  std::size_t grammarPosition_ = 0;
  std::size_t grammarTotal_ = 0;
  void renderGrammar();
};
