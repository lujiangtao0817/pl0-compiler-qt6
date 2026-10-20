#include "CodeEditor.hpp"

#include <QPaintEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextCharFormat>

namespace {

class Pl0Highlighter final : public QSyntaxHighlighter {
 public:
  explicit Pl0Highlighter(QTextDocument* document) : QSyntaxHighlighter(document) {
    keyword_.setForeground(QColor(QStringLiteral("#175CD3")));
    keyword_.setFontWeight(QFont::DemiBold);
    number_.setForeground(QColor(QStringLiteral("#B54708")));
    comment_.setForeground(QColor(QStringLiteral("#667085")));
    comment_.setFontItalic(true);
    procedure_.setForeground(QColor(QStringLiteral("#7F56D9")));
    procedure_.setFontWeight(QFont::DemiBold);
  }

 protected:
  void highlightBlock(const QString& text) override {
    static const QRegularExpression keywords(
        QStringLiteral("\\b(begin|call|const|do|else|end|if|odd|procedure|then|var|while|repeat|until|read|write|writeln|true|false|and|or|not)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression numbers(QStringLiteral("\\b\\d+\\b"));
    static const QRegularExpression procedureName(
        QStringLiteral("\\bprocedure\\s+([A-Za-z][A-Za-z0-9_]*)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression lineComment(QStringLiteral("//.*$"));

    auto apply = [&](const QRegularExpression& expression, const QTextCharFormat& format,
                     int capture = 0) {
      auto matches = expression.globalMatch(text);
      while (matches.hasNext()) {
        const auto match = matches.next();
        setFormat(match.capturedStart(capture), match.capturedLength(capture), format);
      }
    };
    apply(keywords, keyword_);
    apply(numbers, number_);
    apply(procedureName, procedure_, 1);
    apply(lineComment, comment_);
  }

 private:
  QTextCharFormat keyword_;
  QTextCharFormat number_;
  QTextCharFormat comment_;
  QTextCharFormat procedure_;
};

}  // namespace

CodeEditor::CodeEditor(QWidget* parent) : QPlainTextEdit(parent) {
  lineNumberArea_ = new LineNumberArea(this);
  new Pl0Highlighter(document());

  connect(this, &CodeEditor::blockCountChanged, this,
          &CodeEditor::updateLineNumberAreaWidth);
  connect(this, &CodeEditor::updateRequest, this,
          &CodeEditor::updateLineNumberArea);
  connect(this, &CodeEditor::cursorPositionChanged, this,
          &CodeEditor::highlightCurrentLine);

  updateLineNumberAreaWidth(0);
  highlightCurrentLine();
  setLineWrapMode(QPlainTextEdit::NoWrap);
}

int CodeEditor::lineNumberAreaWidth() const {
  int digits = 1;
  int max = std::max(1, blockCount());
  while (max >= 10) {
    max /= 10;
    ++digits;
  }
  int space = 3 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
  return space + 6;
}

void CodeEditor::setLineNumberFont(const QFont& font) {
  lineNumberFont_ = font;
  updateLineNumberAreaWidth();
}

void CodeEditor::updateLineNumberAreaWidth(int) {
  setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void CodeEditor::updateLineNumberArea(const QRect& rect, int dy) {
  if (dy) {
    lineNumberArea_->scroll(0, dy);
  } else {
    lineNumberArea_->update(0, rect.y(), lineNumberArea_->width(), rect.height());
  }

  if (rect.contains(viewport()->rect())) {
    updateLineNumberAreaWidth(0);
  }
}

void CodeEditor::highlightCurrentLine() {
  QList<QTextEdit::ExtraSelection> extraSelections;

  if (!isReadOnly()) {
    QTextEdit::ExtraSelection selection;
    QColor lineColor = QColor(QStringLiteral("#EEF4FF"));
    selection.format.setBackground(lineColor);
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
    selection.cursor = textCursor();
    selection.cursor.clearSelection();
    extraSelections.append(selection);
  }

  setExtraSelections(extraSelections);
}

void CodeEditor::resizeEvent(QResizeEvent* event) {
  QPlainTextEdit::resizeEvent(event);

  QRect cr = contentsRect();
  lineNumberArea_->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void CodeEditor::paintLineNumbers(QPaintEvent* event) {
  QPainter painter(lineNumberArea_);
  painter.fillRect(event->rect(), QColor(QStringLiteral("#F1F5F9")));

  QFont font = lineNumberFont_.family().isEmpty() ? this->font() : lineNumberFont_;
  painter.setFont(font);
  painter.setPen(QColor(QStringLiteral("#98A2B3")));

  QTextBlock block = firstVisibleBlock();
  int blockNumber = block.blockNumber();
  int top = static_cast<int>(blockBoundingGeometry(block).translated(contentOffset()).top());
  int bottom = top + static_cast<int>(blockBoundingRect(block).height());

  while (block.isValid() && top <= event->rect().bottom()) {
    if (block.isVisible() && bottom >= event->rect().top()) {
      QString number = QString::number(blockNumber + 1);
      painter.drawText(0, top, lineNumberArea_->width() - 4,
                       fontMetrics().height(), Qt::AlignRight | Qt::AlignVCenter,
                       number);
    }

    block = block.next();
    top = bottom;
    bottom = top + static_cast<int>(blockBoundingRect(block).height());
    ++blockNumber;
  }
}


LineNumberArea::LineNumberArea(CodeEditor* editor) : QWidget(editor), editor_(editor) {}

QSize LineNumberArea::sizeHint() const {
  return QSize(editor_->lineNumberAreaWidth(), 0);
}

void LineNumberArea::paintEvent(QPaintEvent* event) {
  editor_->paintLineNumbers(event);
}
