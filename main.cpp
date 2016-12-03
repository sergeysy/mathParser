#include <string>
#include <vector>
#include <boost/fusion/adapted.hpp>
#include <boost/variant.hpp>

///////////////////////////////////////////////////////////////////////////////

using Expression = boost::variant<
      double
    , boost::recursive_wrapper<struct UnaryExpression>
    , boost::recursive_wrapper<struct BinaryExpression>
    , boost::recursive_wrapper<struct FunctionCall>
    >;

struct UnaryExpression {
    enum op_t { Plus, Minus, };

    op_t op;
    Expression arg;
};

BOOST_FUSION_ADAPT_STRUCT(UnaryExpression, op, arg)

struct BinaryExpression {
    enum op_t { Plus, Minus, Mul, Div, Mod, Pow, };

    Expression first;
    std::vector<std::pair<op_t, Expression>> ops;
};

BOOST_FUSION_ADAPT_STRUCT(BinaryExpression, first, ops)

struct FunctionCall {
    std::string function;
    std::vector<Expression> args;
};

BOOST_FUSION_ADAPT_STRUCT(FunctionCall, function, args)

///////////////////////////////////////////////////////////////////////////////

#include <boost/spirit/home/x3.hpp>

namespace x3 = boost::spirit::x3;

x3::rule<class identifier, std::string> const identifier;
x3::rule<class simple_expr, Expression> const simple_expr;
x3::rule<class unary_expr, UnaryExpression> const unary_expr;
x3::rule<class function_call, FunctionCall> const function_call;
x3::rule<class binary_expr_1, BinaryExpression> const binary_expr_1;
x3::rule<class binary_expr_2, BinaryExpression> const binary_expr_2;
x3::rule<class binary_expr_3, BinaryExpression> const binary_expr_3;
x3::rule<class expr, Expression> const expr;

struct unary_op_t : x3::symbols<UnaryExpression::op_t> {
    unary_op_t() {
        add("+",   UnaryExpression::Plus);
        add("-",   UnaryExpression::Minus);
    }
} const unary_op;

struct binary_op : x3::symbols<BinaryExpression::op_t> {
    explicit binary_op(int precedence) {
        switch (precedence) {
        case 1:
            add("+", BinaryExpression::Plus);
            add("-", BinaryExpression::Minus);
            return;
        case 2:
            add("*",   BinaryExpression::Mul);
            add("/",   BinaryExpression::Div);
            add("mod", BinaryExpression::Mod);
            return;
        case 3:
            add("**", BinaryExpression::Pow);
            return;
        }
        throw std::runtime_error("Unknown precedence " + std::to_string(precedence));
    }
};

auto const identifier_def = x3::raw[+(x3::alpha | '_')];

auto const simple_expr_def
    = x3::double_
    | '(' >> expr >> ')'
    | unary_expr
    | function_call
    ;

auto const unary_expr_def = unary_op >> simple_expr;

auto const function_call_def = identifier >> '(' >> (expr % ',') >> ')';

auto const binary_expr_1_def = binary_expr_2 >> *(binary_op(1) >> binary_expr_2);
auto const binary_expr_2_def = binary_expr_3 >> *(binary_op(2) >> binary_expr_3);
auto const binary_expr_3_def = simple_expr >> *(binary_op(3) >> simple_expr);

auto const expr_def = binary_expr_1;

BOOST_SPIRIT_DEFINE(
    identifier,
    simple_expr,
    unary_expr,
    function_call,
    binary_expr_1, binary_expr_2, binary_expr_3,
    expr)

Expression parse(const char* input) {
    auto end = input + std::strlen(input);
    Expression result;
    auto ok = phrase_parse(input, end, expr, x3::space, result);
    if (ok && input == end) return result;
    throw std::runtime_error(std::string("Failed at: `") + input + "`");
}

///////////////////////////////////////////////////////////////////////////////

#include <boost/functional/overloaded_function.hpp>

double eval_binary(BinaryExpression::op_t op, double a, double b) { 
    switch (op) {
    case BinaryExpression::Plus: return a + b;
    case BinaryExpression::Minus: return a - b;
    case BinaryExpression::Mul: return a * b;
    case BinaryExpression::Div: return a / b;
    case BinaryExpression::Mod: return (int)a % (int)b;
    case BinaryExpression::Pow: return pow(a, b);
    default: throw std::runtime_error("Unknown operator");
    }
}

double eval(Expression e) {
    auto visitor = boost::make_overloaded_function(
        [](double x) { return x; },
        [](const UnaryExpression& e) -> double {
            auto a = eval(e.arg);
            switch (e.op) {
            case UnaryExpression::Plus: return +a;
            case UnaryExpression::Minus: return -a;
            }
            throw std::runtime_error("Unknown operator");
        },
        [](const FunctionCall& e) -> double {
            auto arg = [&e](int i) { return eval(e.args.at(i)); };
            if (e.function == "abs") return abs(arg(0));
            if (e.function == "sin") return sin(arg(0));
            if (e.function == "cos") return cos(arg(0));
            if (e.function == "pow") return pow(arg(0), arg(1));
            throw std::runtime_error("Unknown function");
        },
        [](const BinaryExpression& e) -> double {   
            auto a = eval(e.first);
            for (auto&& o : e.ops) {
                auto b = eval(o.second);
                a = eval_binary(o.first, a, b);
            }
            return a;
        });
    return boost::apply_visitor(visitor, e);
}

///////////////////////////////////////////////////////////////////////////////

#include <iostream>

int errors;

void test(const char* input, double expected) {
    try {
        auto result = eval(parse(input));
        if (result == expected) return;
        std::cout << input << " = " << expected << " : error, got " << result << '\n';
    }
    catch (std::exception& e) {
        std::cout << input << " : exception: " << e.what() << '\n';
    }
    ++errors;
}

int main() {
    test("0", 0);
    test("1", 1);
    test("9", 9);
    test("10", 10);
    test("+1", 1);
    test("-1", -1);
    test("(1)", 1);
    test("(-1)", -1);
    test("abs(-1)", 1);
    test("sin(0)", 0);
    test("cos(0)", 1);
    test("pow(2, 3)", 8);
    test("---1", -1);
    test("1+20", 21);
    test("1 + 20", 21);
    test("(1+20)", 21);
    test("-2*3", -6);
    test("2*-3", -6);
    test("1++2", 3);
    test("1+20+300", 321);
    test("1+20+300+4000", 4321);
    test("1+10*2", 21);
    test("10*2+1", 21);
    test("(1+20)*2", 42);
    test("2*(1+20)", 42);
    test("(1+2)*(3+4)", 21);
    test("2*3+4*5", 26);
    test("100+2*10+3", 123);
    test("2**3", 8);
    test("2**3*5+2", 42);
    test("5*2**3+2", 42);
    test("2+5*2**3", 42);
    test("1+2**3*10", 81);
    test("2**3+2*10", 28);
    test("5 * 4 + 3 * 2 + 1", 27);
    std::cout << "Done with " << errors << " errors.\n";
}
