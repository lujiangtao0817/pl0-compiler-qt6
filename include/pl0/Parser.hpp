// 文件: Parser.hpp
// 功能: 声明语法分析器, 将 Token 构造为 AST
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "pl0/AST.hpp"
#include "pl0/Diagnostics.hpp"
#include "pl0/Lexer.hpp"
#include "pl0/SymbolTable.hpp"

namespace pl0 {

enum class ParseTraceKind {
  Enter,
  Consume,
  Exit,
  Error,
  Recover,
};

struct ParseTraceEvent {
  ParseTraceKind kind = ParseTraceKind::Enter;
  std::string rule;
  std::string detail;
  SourceRange range;
  int depth = 0;
};

// 类: 递归下降解析 Token 并生成 AST
class Parser {
 public:
  // 构造: 绑定词法器与诊断收集器
  Parser(Lexer& lexer, DiagnosticSink& diagnostics,
         std::vector<ParseTraceEvent>* trace = nullptr);

  // 函数: 解析完整程序
  std::unique_ptr<Program> parse_program();

 private:
  class TraceScope {
   public:
    TraceScope(Parser& parser, std::string_view rule);
    ~TraceScope();

    TraceScope(const TraceScope&) = delete;
    TraceScope& operator=(const TraceScope&) = delete;

   private:
    Parser& parser_;
    std::string rule_;
  };

  void record_trace(ParseTraceKind kind, std::string_view rule,
                    std::string detail, SourceRange range);

  // 工具: 预读/匹配/期望指定 Token
  const Token& peek(std::size_t lookahead = 0);
  Token consume();
  bool match(TokenKind kind);
  Token expect(TokenKind kind, DiagnosticCode code,
               std::string_view message);

  // 工具: Panic 模式同步
  void synchronize(const std::vector<TokenKind>& sync_tokens);

  // 语法子程序: 解析块/声明/语句/表达式
  std::unique_ptr<Block> parse_block();
  void parse_const_declarations(Block& block);
  void parse_var_declarations(Block& block);
  void parse_procedure_declarations(Block& block);
  StmtPtr parse_statement();
  StmtPtr parse_assignment();
  StmtPtr parse_call();
  StmtPtr parse_begin_end();
  StmtPtr parse_if();
  StmtPtr parse_while();
  StmtPtr parse_repeat();
  StmtPtr parse_read();
  StmtPtr parse_write(bool newline);

  // 语法子程序: 表达式层级解析
  ExprPtr parse_expression();
  ExprPtr parse_logic_term();
  ExprPtr parse_logic_factor();
  ExprPtr parse_relation();
  ExprPtr parse_term();
  ExprPtr parse_factor();
  ExprPtr parse_primary();

  // 工具: 解析逗号分隔标识符列表
  std::vector<std::string> parse_identifier_list();

  // 成员: 依赖与状态
  Lexer& lexer_;
  DiagnosticSink& diagnostics_;
  std::vector<ParseTraceEvent>* trace_ = nullptr;
  int trace_depth_ = 0;
  bool panic_mode_ = false;
};

}  // namespace pl0
