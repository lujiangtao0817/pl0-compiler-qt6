#include "AstWidget.hpp"

#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <QGraphicsTextItem>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QWheelEvent>
#include <QPainterPath>
#include <QFontMetricsF>
#include <QDialog>
#include <QTimer>
#include <QComboBox>
#include <QSignalBlocker>
#include <stdexcept>
#include <algorithm>
#include <optional>
#include <type_traits>

namespace {
struct Node {
  QString label;
  std::optional<pl0::SourceRange> range;
  std::vector<Node> children;
  qreal width = 180;
  bool active = false;
  QString production;
  qreal anchor = 0;
};

QString str(const std::string& s) { return QString::fromStdString(s); }

QString lexeme(const pl0::ParseTraceEvent& event) {
  const QString detail = str(event.detail);
  const int first = detail.indexOf('"');
  const int last = detail.lastIndexOf('"');
  return first >= 0 && last > first ? detail.mid(first + 1, last - first - 1) : detail;
}

// Build the concrete E/T/F tree for the exact source tokens, retaining grouping.
// Left-associative operators are folded into left-recursive production nodes.
class GrammarParser {
 public:
  explicit GrammarParser(const std::vector<pl0::ParseTraceEvent>& tokens) : tokens_(tokens) {}
  Node parse() {
    Node tree = expression();
    if (index_ != tokens_.size()) throw std::runtime_error("unsupported expression");
    return tree;
  }
 private:
  const std::vector<pl0::ParseTraceEvent>& tokens_;
  std::size_t index_ = 0;
  QString peek() const { return index_ < tokens_.size() ? lexeme(tokens_[index_]) : QString(); }
  Node terminal() {
    if (index_ >= tokens_.size()) throw std::runtime_error("missing token");
    const auto& event = tokens_[index_++];
    return {lexeme(event), event.range, {}};
  }
  Node production(const QString& label, const QString& rule, std::vector<Node> children) {
    Node node{label, {}, std::move(children)};
    node.production = rule;
    if (!node.children.empty() && node.children.front().range && node.children.back().range)
      node.range = pl0::SourceRange{node.children.front().range->begin, node.children.back().range->end};
    return node;
  }
  Node factor() {
    if (peek() == "(") {
      Node left = terminal();
      Node expr = expression();
      if (peek() != ")") throw std::runtime_error("missing parenthesis");
      Node right = terminal();
      return production("F", "F → ( E )", {std::move(left), std::move(expr), std::move(right)});
    }
    if (peek() == "+" || peek() == "-") {
      Node op = terminal();
      const QString rule = "F → " + op.label + " F";
      Node child = factor();
      return production("F", rule, {std::move(op), std::move(child)});
    }
    if (index_ >= tokens_.size()) throw std::runtime_error("missing operand");
    const QString detail = str(tokens_[index_].detail);
    const bool id = detail.startsWith("identifier ");
    if (!id && !detail.startsWith("number ")) throw std::runtime_error("unsupported operand");
    return production("F", id ? "F → id" : "F → num", {terminal()});
  }
  Node term() {
    Node tree = production("T", "T → F", {factor()});
    while (peek() == "*" || peek() == "/" || peek() == "%") {
      Node op = terminal();
      const QString rule = "T → T " + op.label + " F";
      Node rhs = factor();
      tree = production("T", rule, {std::move(tree), std::move(op), std::move(rhs)});
    }
    return tree;
  }
  Node expression() {
    Node tree = production("E", "E → T", {term()});
    while (peek() == "+" || peek() == "-") {
      Node op = terminal();
      const QString rule = "E → E " + op.label + " T";
      Node rhs = term();
      tree = production("E", rule, {std::move(tree), std::move(op), std::move(rhs)});
    }
    return tree;
  }
};

std::size_t productionCount(const Node& node) {
  std::size_t total = node.production.isEmpty() ? 0 : 1;
  for (const auto& child : node.children) total += productionCount(child);
  return total;
}

void reveal(Node& node, std::size_t& remaining, QString& rule) {
  if (!node.production.isEmpty()) {
    if (remaining == 0) { node.children.clear(); return; }
    --remaining;
    if (remaining == 0) { node.active = true; rule = node.production; }
  }
  for (auto& child : node.children) reveal(child, remaining, rule);
}

QString binary(pl0::BinaryOp op) {
  switch (op) {
    case pl0::BinaryOp::Add: return "+";
    case pl0::BinaryOp::Subtract: return "-";
    case pl0::BinaryOp::Multiply: return "*";
    case pl0::BinaryOp::Divide: return "/";
    case pl0::BinaryOp::Modulo: return "%";
    case pl0::BinaryOp::Equal: return "=";
    case pl0::BinaryOp::NotEqual: return "<>";
    case pl0::BinaryOp::Less: return "<";
    case pl0::BinaryOp::LessEqual: return "<=";
    case pl0::BinaryOp::Greater: return ">";
    case pl0::BinaryOp::GreaterEqual: return ">=";
    case pl0::BinaryOp::And: return "and";
    case pl0::BinaryOp::Or: return "or";
  }
  return "?";
}

Node expression(const pl0::Expression& expr) {
  Node node{QString(), expr.range, {}};
  std::visit([&](const auto& value) {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, pl0::NumberLiteral>)
      node.label = QObject::tr("整数 %1").arg(value.value);
    else if constexpr (std::is_same_v<T, pl0::BooleanLiteral>)
      node.label = value.value ? "true" : "false";
    else if constexpr (std::is_same_v<T, pl0::IdentifierExpr>)
      node.label = QObject::tr("变量 %1").arg(str(value.name));
    else if constexpr (std::is_same_v<T, pl0::ArrayAccessExpr>) {
      node.label = QObject::tr("数组访问 %1[ ]").arg(str(value.name));
      if (value.index) node.children.push_back(expression(*value.index));
    } else if constexpr (std::is_same_v<T, pl0::BinaryExpr>) {
      node.label = QObject::tr("二元运算 %1").arg(binary(value.op));
      if (value.lhs) node.children.push_back(expression(*value.lhs));
      if (value.rhs) node.children.push_back(expression(*value.rhs));
    } else if constexpr (std::is_same_v<T, pl0::UnaryExpr>) {
      QString op;
      switch (value.op) {
        case pl0::UnaryOp::Positive: op = "+"; break;
        case pl0::UnaryOp::Negative: op = "-"; break;
        case pl0::UnaryOp::Not: op = "not"; break;
        case pl0::UnaryOp::Odd: op = "odd"; break;
      }
      node.label = QObject::tr("一元运算 %1").arg(op);
      if (value.operand) node.children.push_back(expression(*value.operand));
    } else if constexpr (std::is_same_v<T, pl0::CallExpr>) {
      node.label = QObject::tr("调用表达式 %1").arg(str(value.callee));
      for (const auto& arg : value.arguments) if (arg) node.children.push_back(expression(*arg));
    }
  }, expr.value);
  return node;
}

Node statements(const QString& label, const std::vector<pl0::StmtPtr>& list);
Node statement(const pl0::Statement& stmt) {
  Node node{QString(), stmt.range, {}};
  std::visit([&](const auto& value) {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, pl0::AssignmentStmt>) {
      QString op;
      switch (value.op) {
        case pl0::AssignmentOperator::Assign: op = ":="; break;
        case pl0::AssignmentOperator::AddAssign: op = "+="; break;
        case pl0::AssignmentOperator::SubAssign: op = "-="; break;
        case pl0::AssignmentOperator::MulAssign: op = "*="; break;
        case pl0::AssignmentOperator::DivAssign: op = "/="; break;
        case pl0::AssignmentOperator::ModAssign: op = "%="; break;
      }
      node.label = QObject::tr("赋值 %1").arg(op);
      Node target{QObject::tr("目标 %1%2").arg(str(value.target), value.index ? "[ ]" : ""), {}, {}};
      if (value.index) target.children.push_back(expression(*value.index));
      node.children.push_back(std::move(target));
      if (value.value) node.children.push_back(expression(*value.value));
    } else if constexpr (std::is_same_v<T, pl0::CallStmt>) {
      node.label = QObject::tr("过程调用 %1").arg(str(value.callee));
      for (const auto& arg : value.arguments) if (arg) node.children.push_back(expression(*arg));
    } else if constexpr (std::is_same_v<T, pl0::IfStmt>) {
      node.label = "if";
      if (value.condition) node.children.push_back({QObject::tr("条件"), {}, {expression(*value.condition)}});
      node.children.push_back(statements("then", value.then_branch));
      if (!value.else_branch.empty()) node.children.push_back(statements("else", value.else_branch));
    } else if constexpr (std::is_same_v<T, pl0::WhileStmt>) {
      node.label = "while";
      if (value.condition) node.children.push_back({QObject::tr("条件"), {}, {expression(*value.condition)}});
      node.children.push_back(statements(QObject::tr("循环体"), value.body));
    } else if constexpr (std::is_same_v<T, pl0::RepeatStmt>) {
      node.label = "repeat-until";
      node.children.push_back(statements(QObject::tr("循环体"), value.body));
      if (value.condition) node.children.push_back({"until", {}, {expression(*value.condition)}});
    } else if constexpr (std::is_same_v<T, pl0::ReadStmt>) {
      node.label = "read";
      for (const auto& target : value.targets) node.children.push_back({str(target), {}, {}});
    } else if constexpr (std::is_same_v<T, pl0::WriteStmt>) {
      node.label = value.newline ? "writeln" : "write";
      for (const auto& val : value.values) if (val) node.children.push_back(expression(*val));
    } else if constexpr (std::is_same_v<T, std::vector<pl0::StmtPtr>>) {
      node = statements(QObject::tr("复合语句"), value);
      node.range = stmt.range;
    }
  }, stmt.value);
  return node;
}

Node statements(const QString& label, const std::vector<pl0::StmtPtr>& list) {
  Node node{label, {}, {}};
  for (const auto& stmt : list) if (stmt) node.children.push_back(statement(*stmt));
  return node;
}

Node variable(const pl0::VarDecl& var) {
  return {QObject::tr("%1 %2%3").arg(var.type == pl0::VarType::Boolean ? "bool" : "var",
      str(var.name), var.array_size ? QString("[%1]").arg(*var.array_size) : QString()), var.range, {}};
}

Node block(const pl0::Block& b) {
  Node node{QObject::tr("分程序"), {}, {}};
  for (const auto& c : b.consts)
    node.children.push_back({QString("const %1 = %2").arg(str(c.name)).arg(c.value), c.range, {}});
  for (const auto& var : b.vars) node.children.push_back(variable(var));
  for (const auto& p : b.procedures) {
    Node proc{QObject::tr("过程 %1").arg(str(p.name)), p.range, {}};
    if (!p.parameters.empty()) {
      Node params{QObject::tr("参数"), {}, {}};
      for (const auto& param : p.parameters) params.children.push_back(variable(param));
      proc.children.push_back(std::move(params));
    }
    if (p.body) proc.children.push_back(block(*p.body));
    node.children.push_back(std::move(proc));
  }
  node.children.push_back(statements(QObject::tr("语句序列"), b.statements));
  return node;
}

qreal measure(Node& node) {
  qreal width = 0;
  for (auto& child : node.children) width += measure(child) + 24;
  node.width = std::max(qreal(180), width - 24);
  return node.width;
}

QFont bookFont(const Node& node) {
  QFont font(QStringLiteral("Times New Roman"), 17);
  font.setStyleHint(QFont::Serif);
  font.setItalic(!node.production.isEmpty() ||
                 (!node.label.isEmpty() && node.label.front().isLetter()));
  return font;
}

void measureBook(Node& node) {
  const qreal textWidth = QFontMetricsF(bookFont(node)).horizontalAdvance(node.label) + 16;
  node.width = std::max(qreal(38), textWidth);
  node.anchor = node.width / 2;
  if (node.children.empty()) return;
  qreal extent = 0;
  qreal first = 0;
  qreal last = 0;
  for (std::size_t i = 0; i < node.children.size(); ++i) {
    auto& child = node.children[i];
    measureBook(child);
    if (i == 0) first = child.anchor;
    last = extent + child.anchor;
    extent += child.width + 28;
  }
  node.width = std::max(node.width, extent - 28);
  node.anchor = (first + last) / 2;
}

void drawBook(QGraphicsScene* scene, const Node& node, qreal left, qreal y) {
  const qreal x = left + node.anchor;
  const QFont font = bookFont(node);
  const qreal width = QFontMetricsF(font).horizontalAdvance(node.label) + 12;
  auto* rect = scene->addRect(x - width / 2, y, width, 32, QPen(Qt::NoPen), QBrush(Qt::NoBrush));
  rect->setData(0, node.label);
  rect->setData(5, node.production);
  rect->setToolTip(node.production.isEmpty() ? node.label : node.production);
  if (node.range) {
    rect->setFlag(QGraphicsItem::ItemIsSelectable);
    rect->setData(1, QVariant::fromValue<qulonglong>(node.range->begin.line));
    rect->setData(2, QVariant::fromValue<qulonglong>(node.range->begin.column));
    rect->setData(3, QVariant::fromValue<qulonglong>(node.range->end.line));
    rect->setData(4, QVariant::fromValue<qulonglong>(node.range->end.column));
  }
  auto* text = new QGraphicsTextItem(rect);
  text->setFont(font);
  text->setPlainText(node.label);
  text->setDefaultTextColor(QColor(node.active ? "#9B3030" : "#202020"));
  text->setPos(x - text->boundingRect().width() / 2,
               y + (32 - text->boundingRect().height()) / 2);
  text->setAcceptedMouseButtons(Qt::NoButton);
  qreal childLeft = left;
  for (std::size_t i = 0; i < node.children.size(); ++i) {
    const auto& child = node.children[i];
    qreal childOrigin = childLeft;
    // In E -> E + T and T -> T * F, place the terminal operator
    // directly below its parent, as in printed grammar diagrams.
    if (node.children.size() == 3 && i == 1 && child.production.isEmpty() && child.children.empty())
      childOrigin = x - child.anchor;
    const qreal childX = childOrigin + child.anchor;
    auto* edge = scene->addLine(x, y + 35, childX, y + 71, QPen(QColor("#505050"), 1.1));
    edge->setZValue(-1);
    drawBook(scene, child, childOrigin, y + 74);
    childLeft += child.width + 28;
  }
}

void fitCanvas(QGraphicsView* view, const QRectF& rect) {
  view->fitInView(rect, Qt::KeepAspectRatio);
  // Keep a small tree at a readable print-like scale instead of filling the
  // entire screen with a single letter during the first derivation step.
  if (view->transform().m11() > 1.35) {
    const qreal correction = 1.35 / view->transform().m11();
    view->scale(correction, correction);
  }
}

void draw(QGraphicsScene* scene, const Node& node, qreal left, qreal y, bool textbook = false) {
  const qreal x = left + node.width / 2;
  auto* rect = scene->addRect(x - 80, y, 160, 52,
      textbook ? QPen(Qt::NoPen) : QPen(QColor("#93B4DB")),
      textbook ? QBrush(Qt::NoBrush) : QBrush(QColor(node.range ? "#E6EFFF" : "#F1F5F9")));
  rect->setData(0, node.label);
  rect->setData(5, node.production);
  rect->setToolTip(node.label + (node.production.isEmpty() ? QString() : "\n" + node.production)
      + (node.range ? QObject::tr("\n源码 %1:%2，点击定位")
      .arg(node.range->begin.line).arg(node.range->begin.column) : QObject::tr("\n结构分组")));
  if (node.range) {
    rect->setFlag(QGraphicsItem::ItemIsSelectable);
    rect->setData(1, QVariant::fromValue<qulonglong>(node.range->begin.line));
    rect->setData(2, QVariant::fromValue<qulonglong>(node.range->begin.column));
    rect->setData(3, QVariant::fromValue<qulonglong>(node.range->end.line));
    rect->setData(4, QVariant::fromValue<qulonglong>(node.range->end.column));
  }
  auto* text = new QGraphicsTextItem(rect);
  text->setDefaultTextColor(QColor(textbook ? (node.active ? "#D92D20" : "#111111") : "#123B6D"));
  text->setPlainText(QFontMetricsF(text->font()).elidedText(node.label, Qt::ElideRight, 144));
  text->setPos(x - 74, y + 12);
  if (textbook) text->setPos(x - text->boundingRect().width() / 2, y + 12);
  text->setAcceptedMouseButtons(Qt::NoButton);
  qreal childLeft = left;
  for (const auto& child : node.children) {
    QPainterPath path(QPointF(x, y + 52));
    const qreal childX = childLeft + child.width / 2;
    if (textbook) {
      path = QPainterPath(QPointF(x, y + 38));
      path.lineTo(childX, y + 116);
    } else path.cubicTo(x, y + 78, childX, y + 78, childX, y + 104);
    auto* edge = scene->addPath(path, QPen(QColor(textbook ? "#333333" : "#94A3B8"), 1.5));
    edge->setZValue(-1);
    draw(scene, child, childLeft, y + 104, textbook);
    childLeft += child.width + 24;
  }
}

class View final : public QGraphicsView {
 public:
  using QGraphicsView::QGraphicsView;
 protected:
  void resizeEvent(QResizeEvent* event) override {
    QGraphicsView::resizeEvent(event);
    if (scene() && !scene()->items().empty())
      fitCanvas(this, scene()->sceneRect());
  }
  void wheelEvent(QWheelEvent* event) override {
    if (event->angleDelta().y() == 0) { event->ignore(); return; }
    const qreal factor = event->angleDelta().y() > 0 ? 1.15 : 1 / 1.15;
    const qreal next = transform().m11() * factor;
    if (next >= 0.05 && next <= 3) scale(factor, factor);
    event->accept();
  }
};
} // namespace

AstWidget::AstWidget(QWidget* parent, bool textbook, bool grammar)
    : QWidget(parent), textbook_(textbook), grammar_(grammar) {
  setObjectName(grammar ? "grammarPanel" : textbook ? "syntaxTreePanel" : "astPanel");
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(4, 4, 4, 4);
  auto* controls = new QHBoxLayout;
  auto* fit = new QPushButton(tr("适应窗口"), this);
  auto* actual = new QPushButton(tr("原始大小"), this);
  auto* large = new QPushButton(tr("大图查看"), this);
  large->setObjectName("astLarge");
  controls->addWidget(fit);
  controls->addWidget(actual);
  controls->addWidget(large);
  controls->addStretch();
  layout->addLayout(controls);
  if (grammar) {
    auto* rules = new QLabel(tr("E → E + T | E - T | T\nT → T * F | T / F | T % F | F\nF → ( E ) | id | num | + F | - F"), this);
    rules->setWordWrap(true);
    layout->addWidget(rules);
    expressionChoice_ = new QComboBox(this);
    expressionChoice_->setObjectName("grammarExpression");
    layout->addWidget(expressionChoice_);
    grammarTimer_ = new QTimer(this);
    grammarTimer_->setInterval(700);
    connect(grammarTimer_, &QTimer::timeout, this, [this] { grammarCommand(1); });
    connect(expressionChoice_, &QComboBox::currentIndexChanged, this, [this] {
      grammarTimer_->stop();
      grammarPosition_ = 0;
      renderGrammar();
    });
    connect(this, &AstWidget::replayRequested, this, &AstWidget::grammarCommand);
  }
  if (textbook) {
    auto* replay = new QHBoxLayout;
    const QStringList labels{tr("上一步"), tr("下一步"), tr("播放"), tr("暂停"), tr("重置"), tr("全部")};
    for (int i = 0; i < labels.size(); ++i) {
      auto* button = new QPushButton(labels[i], this);
      replay->addWidget(button);
      connect(button, &QPushButton::clicked, this, [this, i] { Q_EMIT replayRequested(i); });
    }
    layout->addLayout(replay);
  }
  hint_ = new QLabel(tr("编译成功后显示 AST · 滚轮缩放 · 拖动画布 · 点击蓝色节点定位源码"), this);
  hint_->setWordWrap(true);
  layout->addWidget(hint_);
  scene_ = new QGraphicsScene(this);
  view_ = new View(scene_, this);
  view_->setObjectName(grammar ? "grammarTreeView" : textbook ? "syntaxTreeView" : "astView");
  view_->setRenderHint(QPainter::Antialiasing);
  view_->setDragMode(QGraphicsView::ScrollHandDrag);
  view_->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
  view_->setBackgroundBrush(QColor("#FFFFFF"));
  layout->addWidget(view_, 1);
  connect(fit, &QPushButton::clicked, this, [this] {
    if (!scene_->items().empty()) fitCanvas(view_, scene_->sceneRect());
  });
  connect(actual, &QPushButton::clicked, view_, &QGraphicsView::resetTransform);
  connect(large, &QPushButton::clicked, this, [this] {
    QDialog dialog(this);
    dialog.setWindowTitle(textbook_ ? tr("教材风格分析树 · 滚轮缩放，拖动画布")
                                   : tr("抽象语法树 AST · 滚轮缩放，拖动画布"));
    if (grammar_) dialog.setWindowTitle(tr("表达式文法树 E / T / F"));
    dialog.resize(1200, 800);
    auto* layout = new QVBoxLayout(&dialog);
    if (grammar_) layout->addWidget(new QLabel(tr("E → E + T | E - T | T     T → T * F | T / F | T % F | F\nF → ( E ) | id | num | + F | - F"), &dialog));
    if (textbook_) {
      auto* replay = new QHBoxLayout;
      const QStringList labels{tr("上一步"), tr("下一步"), tr("播放"), tr("暂停"), tr("重置"), tr("全部")};
      for (int i = 0; i < labels.size(); ++i) {
        auto* button = new QPushButton(labels[i], &dialog);
        replay->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, i] { Q_EMIT replayRequested(i); });
      }
      layout->addLayout(replay);
    }
    auto* fit = new QPushButton(tr("适应窗口"), &dialog);
    layout->addWidget(fit);
    auto* canvas = new View(scene_, &dialog);
    canvas->setRenderHint(QPainter::Antialiasing);
    canvas->setDragMode(QGraphicsView::ScrollHandDrag);
    canvas->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    canvas->setBackgroundBrush(Qt::white);
    layout->addWidget(canvas);
    connect(fit, &QPushButton::clicked, canvas, [this, canvas] {
      fitCanvas(canvas, scene_->sceneRect());
    });
    if (textbook_) connect(scene_, &QGraphicsScene::sceneRectChanged, canvas,
        [canvas](const QRectF& rect) { fitCanvas(canvas, rect); });
    dialog.show();
    fitCanvas(canvas, scene_->sceneRect());
    dialog.exec();
  });
  connect(scene_, &QGraphicsScene::selectionChanged, this, [this] {
    const auto items = scene_->selectedItems();
    if (items.empty()) return;
    const auto* item = items.front();
    hint_->setText(tr("%1 · 源码 %2:%3").arg(item->data(0).toString())
        .arg(item->data(1).toULongLong()).arg(item->data(2).toULongLong()));
    if (grammar_ && !item->data(5).toString().isEmpty())
      hint_->setText(hint_->text() + " · " + item->data(5).toString());
    Q_EMIT sourceActivated(item->data(1).toULongLong(), item->data(2).toULongLong(),
                           item->data(3).toULongLong(), item->data(4).toULongLong());
  });
}

void AstWidget::setParseTrace(const std::vector<pl0::ParseTraceEvent>& events, std::size_t position) {
  scene_->clear();
  hint_->setText(tr("分析事件 %1 / %2 · 红色为当前节点 · 规则展开与词法单元来自实际分析记录")
      .arg(static_cast<qulonglong>(position)).arg(static_cast<qulonglong>(events.size())));
  if (position == 0) return;
  Node root{QString(), {}, {}};
  std::vector<Node*> stack{&root};
  Node* current = nullptr;
  for (std::size_t i = 0; i < std::min(position, events.size()); ++i) {
    const auto& event = events[i];
    if (event.kind == pl0::ParseTraceKind::Exit) {
      if (stack.size() > 1) { current = stack.back(); stack.pop_back(); }
      continue;
    }
    QString label = str(event.rule.empty() ? event.detail : event.rule);
    if (event.kind == pl0::ParseTraceKind::Consume) {
      const int first = label.indexOf('"');
      const int last = label.lastIndexOf('"');
      if (first >= 0 && last > first) label = label.mid(first + 1, last - first - 1);
    }
    const std::pair<const char*, const char*> names[]{
        {"program", "程序"}, {"block", "分程序"}, {"statement", "语句"},
        {"expression", "表达式"}, {"logic-term", "逻辑项"}, {"logic-factor", "逻辑因子"},
        {"relation", "关系式"}, {"term", "项"}, {"factor", "因子"}, {"primary", "基本项"},
        {"assignment", "赋值语句"}, {"begin-end", "复合语句"}, {"const-declarations", "常量声明"},
        {"var-declarations", "变量声明"}, {"procedure-declarations", "过程声明"}};
    if (event.kind == pl0::ParseTraceKind::Enter)
      for (const auto& name : names) if (label == QLatin1String(name.first)) {
        label = QString::fromUtf8(name.second); break;
      }
    stack.back()->children.push_back({label, event.range, {}});
    current = &stack.back()->children.back();
    if (event.kind == pl0::ParseTraceKind::Enter) stack.push_back(current);
  }
  if (current) current->active = true;
  if (root.children.empty()) return;
  // The synthetic container is not a grammar node and is never drawn.
  Node& tree = root.children.front();
  measure(tree);
  draw(scene_, tree, 0, 0, true);
  scene_->setSceneRect(scene_->itemsBoundingRect().adjusted(-24, -24, 24, 24));
  fitPending_ = true;
  if (isVisible()) fitCanvas(view_, scene_->sceneRect());
}

void AstWidget::setProgram(const pl0::Program* program) {
  scene_->clear();
  view_->resetTransform();
  hint_->setText(tr("编译成功后显示 AST · 滚轮缩放 · 拖动画布 · 点击蓝色节点定位源码"));
  if (!program) return;
  Node root{tr("程序"), {}, {block(program->block)}};
  measure(root);
  draw(scene_, root, 0, 0);
  scene_->setSceneRect(scene_->itemsBoundingRect().adjusted(-24, -24, 24, 24));
  fitCanvas(view_, scene_->sceneRect());
  fitPending_ = true;
  if (isVisible()) {
    QTimer::singleShot(0, this, [this] {
      if (isVisible() && fitPending_) {
        fitCanvas(view_, scene_->sceneRect());
        fitPending_ = false;
      }
    });
  }
}

void AstWidget::setGrammarTrace(const std::vector<pl0::ParseTraceEvent>& events) {
  grammarTimer_->stop();
  expressions_.clear();
  QSignalBlocker blocker(expressionChoice_);
  expressionChoice_->clear();
  std::vector<std::pair<int, std::vector<pl0::ParseTraceEvent>>> open;
  for (const auto& event : events) {
    if (event.kind == pl0::ParseTraceKind::Enter && event.rule == "expression")
      open.push_back({event.depth, {}});
    else if (event.kind == pl0::ParseTraceKind::Consume)
      for (auto& expression : open) expression.second.push_back(event);
    else if (event.kind == pl0::ParseTraceKind::Exit && event.rule == "expression" && !open.empty()) {
      auto tokens = std::move(open.back().second);
      open.pop_back();
      try {
        GrammarParser parser(tokens);
        parser.parse();
        QStringList words;
        for (const auto& token : tokens) words.append(lexeme(token));
        expressionChoice_->addItem(tr("%1:%2  %3").arg(tokens.front().range.begin.line)
            .arg(tokens.front().range.begin.column).arg(words.join(' ')));
        expressions_.push_back(std::move(tokens));
      } catch (const std::exception&) {
        // Logic, relational, array and call syntax are outside the displayed
        // arithmetic grammar; never present them as valid E/T/F derivations.
      }
    }
  }
  grammarPosition_ = 0;
  renderGrammar();
  grammarPosition_ = grammarTotal_;
  renderGrammar();
}

void AstWidget::grammarCommand(int command) {
  if (expressions_.empty()) return;
  if (command != 1) grammarTimer_->stop();
  switch (command) {
    case 0: if (grammarPosition_ > 0) --grammarPosition_; break;
    case 1: if (grammarPosition_ < grammarTotal_) ++grammarPosition_; break;
    case 2:
      if (grammarPosition_ == grammarTotal_) grammarPosition_ = 0;
      grammarTimer_->start();
      break;
    case 3: break;
    case 4: grammarPosition_ = 0; break;
    case 5: grammarPosition_ = grammarTotal_; break;
  }
  if (grammarPosition_ == grammarTotal_) grammarTimer_->stop();
  renderGrammar();
}

void AstWidget::renderGrammar() {
  scene_->clear();
  const int selected = expressionChoice_->currentIndex();
  if (selected < 0 || selected >= static_cast<int>(expressions_.size())) {
    grammarTotal_ = 0;
    hint_->setText(tr("编译后选择算术表达式。此页仅按上方文法推导；逻辑、关系、数组及调用请查看整程序分析树。"));
    return;
  }
  Node root = GrammarParser(expressions_[static_cast<std::size_t>(selected)]).parse();
  grammarTotal_ = productionCount(root);
  grammarPosition_ = std::min(grammarPosition_, grammarTotal_);
  auto remaining = grammarPosition_;
  QString rule = tr("开始符号 E");
  reveal(root, remaining, rule);
  hint_->setText(tr("最左推导 %1 / %2 · %3 · 叶子保留源码实际标识符和数值")
      .arg(static_cast<qulonglong>(grammarPosition_)).arg(static_cast<qulonglong>(grammarTotal_)).arg(rule));
  measureBook(root);
  drawBook(scene_, root, 0, 0);
  scene_->setSceneRect(scene_->itemsBoundingRect().adjusted(-40, -32, 40, 32));
  fitPending_ = true;
  if (isVisible()) fitCanvas(view_, scene_->sceneRect());
}

void AstWidget::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  if (fitPending_) QTimer::singleShot(0, this, [this] {
    if (isVisible()) {
      fitCanvas(view_, scene_->sceneRect());
      fitPending_ = false;
    }
  });
}

void AstWidget::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  QTimer::singleShot(0, this, [this] {
    if (isVisible() && !scene_->items().empty())
      fitCanvas(view_, scene_->sceneRect());
  });
}
