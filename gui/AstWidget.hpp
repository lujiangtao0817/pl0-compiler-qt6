#pragma once

#include <QWidget>
#include "pl0/AST.hpp"

class QGraphicsScene;
class QGraphicsView;
class QLabel;
class QShowEvent;
class QResizeEvent;

class AstWidget final : public QWidget {
  Q_OBJECT
 public:
  explicit AstWidget(QWidget* parent = nullptr);
  void setProgram(const pl0::Program* program);

 protected:
  void showEvent(QShowEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

 Q_SIGNALS:
  void sourceActivated(qulonglong beginLine, qulonglong beginColumn,
                       qulonglong endLine, qulonglong endColumn);

 private:
  QGraphicsScene* scene_;
  QGraphicsView* view_;
  QLabel* hint_;
  bool fitPending_ = true;
};
