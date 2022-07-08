// rsolver
// (c) Copyright 2022, Recursive Pizza

#include <stdio.h>
#include <stdarg.h>
#include <string>
#include <string.h>
#include <vector>
#include <iostream>

// Same exit codes as minisat
// Except it uses 10 for EXIT_SATISFIABLE
enum { EXIT_COMMAND_LINE_FAIL = 0, EXIT_CANNOT_READ_INPUT = 1, EXIT_CANNOT_PARSE_INPUT = 3, EXIT_SATISFIABLE = 0, EXIT_UNSATISFIABLE = 20 };

static void usage()
{
	std::cerr << "Usage: rsolver '<logic-expression>'\n"
		"\n"
		"A toy SAT (boolean SATisfiability) solver\n"
		"https://en.wikipedia.org/wiki/Satisfiability\n"
		"\n"
		"You can put the logic expression on the command line (in quotes) or send it via stdin\n"
		"\n"
		"Example expressions:\n"
		"a & ~b\n"
		"x & ~x\n"
		"mike & sally & ~peter\n"
		"~(mike & sally) & ~peter\n"
		"\n"
		"The following are supported: &=and, |=or, ~=not, ()=brackets, letters=literals\n"
		"There is no attempt at optimization or avoiding recursion\n";
	exit(EXIT_COMMAND_LINE_FAIL);
}

//-----------------------------------------------------------------------------
// Bool Util

static std::string boolToString(const bool b) {
	return b ? "True" : "False";
}

static bool gBools[] = { true, false };
static int gnBools = sizeof(gBools) / sizeof(gBools[0]);

//-----------------------------------------------------------------------------
// String Util

enum { MAX_LINE = 5 * 1024 };

static size_t strlcat(char *dst, const char *src, const size_t bufsize) {
	size_t		len_dst;
	size_t		len_src;
	size_t		len_result;

	if (dst == NULL) return 0;
	len_dst = len_result = strlen(dst);
	if (src == NULL) return len_result;
	len_src = strlen(src);
	len_result += len_src;

	if (len_dst + len_src >= bufsize) {
		len_src = bufsize - (len_dst + 1);
	}
	
	if (len_src > 0) {
		memcpy(dst + len_dst, src, len_src);
		dst[len_dst + len_src] = '\0';
	}

	return len_result;
}

static size_t strlcpy(char *dst, const char *src, const size_t bufsize) {
	size_t		len;

	if (src == NULL) return 0;
	len = strlen(src);
	if (dst == NULL) return len;

	if (len >= bufsize) len = bufsize - 1;
	memcpy(dst, src, len);
	dst[len] = '\0';
	return len;
}

void flatten(const int argc, char *argv[], const int optind, char *buf, const size_t size)
{
	buf[0] = '\0';

	for (int i = optind; i < argc; i++) {
		if (buf[0]) strlcat(buf, " ", size);
		strlcat(buf, argv[i], size);
	}
}

static void chomp(char *s) {
	char	*p;

	if ((p = strchr(s, '\n')) != NULL) {
		*p = '\0';
	}
}

//-----------------------------------------------------------------------------
// Parse

enum TokType { TT_Unknown, TT_And, TT_Or, TT_Not, TT_Literal, TT_OpenBracket, TT_CloseBracket, TT_Space, TT_Eof };

static std::string typeToString(const TokType type) {
	switch(type) {
	case TT_Unknown: return "Unknown";
	case TT_And: return "&";
	case TT_Or: return "|";
	case TT_Not: return "~";
	case TT_Literal: return "Literal";
	case TT_OpenBracket: return "(";
	case TT_CloseBracket: return ")";
	case TT_Space: return "Space";	// Should never happen
	case TT_Eof: return "Eof";
	}
	return "NotHandled";	// Should never happen
}

class Token {
	TokType mType;
	std::string mLiteral;
	friend class Tokenizer;

public:
	Token(const TokType type) {
		mType = type;
	}

	Token() {
		mType = TT_Unknown;
	}

	bool isLiteral() const { return mType == TT_Literal; }
	bool isCloseBracket() const { return mType == TT_CloseBracket; }
	bool isEof() const { return mType == TT_Eof; }

	TokType getType() const { return mType; }
	std::string getLiteral() const { return mLiteral; }

	std::string toString() const {
		if (isLiteral()) {
			return mLiteral;
		}
		else {
			return typeToString(mType);
		}
	}
};

typedef std::vector<Token> Tokens;

class Tokenizer {
	char mszLine[MAX_LINE];
	char *mpPosition = mszLine;

public:
	Tokenizer(const char *szLine) {
		strlcpy(mszLine, szLine, sizeof(mszLine));
	}

	Token getToken() {
		Token tok;

		for (;;) {
			if (*mpPosition == '\0') return TT_Eof;

			if (isspace(*mpPosition)) {
				mpPosition++;
				continue;
			}

			if (*mpPosition == '&') {
				mpPosition++;
				return TT_And;
			}
			else if (*mpPosition == '|') {
				mpPosition++;
				return TT_Or;
			}
			else if (*mpPosition == '~') {
				mpPosition++;
				return TT_Not;
			}
			else if (*mpPosition == '(') {
				mpPosition++;
				return TT_OpenBracket;
			}
			else if (*mpPosition == ')') {
				mpPosition++;
				return TT_CloseBracket;
			}
			else if (isalpha(*mpPosition)) {
				tok.mType = TT_Literal;
				tok.mLiteral = *mpPosition++;
				for (;;) {
					if (!isalnum(*mpPosition)) {
						return tok;
					}
					tok.mLiteral += *mpPosition++;
				}
			}
			else {
				mpPosition++;
				return TT_Unknown;
			}
		}

		return tok;
	}
};

static Tokens parseLine(const char *szLine) {
	Tokenizer tokenizer(szLine);
	Tokens tokens;

	for (;;) {
		const Token tok = tokenizer.getToken();
		if (tok.isEof()) break;
		tokens.push_back(tok);
	}

	return tokens;
}

static void printTokens(const Tokens &tokens) {
	std::string out;
	for (Tokens::const_iterator it = tokens.begin(); it != tokens.end(); it++) {
		if (!out.empty()) out += " ";
		out += it->toString();
	}
	std::cout << out << std::endl;
}

//-----------------------------------------------------------------------------
// Literals

class Literal {
	std::string mName;
	bool mValue;
public:

	Literal(const std::string name) {
		mName = name;
		mValue = false;
	}

	bool isMatch(const std::string name) const {
		return name.compare(mName) == 0;
	}

	std::string getName() const { return mName; }

	void setBool(const bool b) { mValue = b; }
	bool getBool() const { return mValue; }

	std::string toString() const {
		return mName + "=" + boolToString(mValue);
	}
};

typedef std::vector<Literal> Literals;

static std::string literalsToString(const Literals &frozen, const Literals &thawed) {
	std::string out;

	for (Literals::const_iterator it = frozen.begin(); it != frozen.end(); it++) {
		if (!out.empty()) out += " ";
		out += it->toString();
	}

	for (Literals::const_iterator it = thawed.begin(); it != thawed.end(); it++) {
		if (!out.empty()) out += " ";
		out += it->toString();
	}

	return out;
}

static std::string literalsWithoutValues(const Literals &frozen, const Literals &thawed) {
	std::string out;

	for (Literals::const_iterator it = frozen.begin(); it != frozen.end(); it++) {
		if (!out.empty()) out += " ";
		out += it->getName();
	}

	for (Literals::const_iterator it = thawed.begin(); it != thawed.end(); it++) {
		if (!out.empty()) out += " ";
		out += it->getName();
	}

	return out;
}

static void printLiteralsWithoutValues(const Literals &frozen, const Literals &thawed) {
	std::cout << "Literals: " << literalsWithoutValues(frozen, thawed) << std::endl;
}

// Of course a std::map<> would be faster but we need to maintain the order
static Literals::const_iterator findLiteral(const Literals &literals, const std::string target) {
	for (Literals::const_iterator it = literals.begin(); it != literals.end(); it++) {
		if (it->isMatch(target)) return it;
	}
	return literals.end();
}

static Literals::const_iterator findLiteral(const Literals &frozen, const Literals &thawed, const std::string target) {
	Literals::const_iterator it = findLiteral(frozen, target);
	if (it != frozen.end()) return it;
	
	return findLiteral(thawed, target);
}

static Literals getLiterals(const Tokens &tokens) {
	Literals literals;

	for (Tokens::const_iterator it = tokens.begin(); it != tokens.end(); it++) {
		if (it->isLiteral()) {
			const std::string name = it->getLiteral();
			if (findLiteral(literals, name) != literals.end()) continue;
			Literal lit(name);
			literals.push_back(lit);
		}
	}
	return literals;
}

static Literals butFirst(const Literals &in) {
	Literals out;

	for (int i = 1; i < (int) in.size(); i++) {
		out.push_back(in[i]);
	}

	return out;
}

static Literals appendFirst(const Literals &base, const Literals &more) {
	Literals out = base;

	if (more.size() == 0) return base;
	out.push_back(more[0]);
	return out;
}

//-----------------------------------------------------------------------------
// Eval
//    <expr> = <clause> <op> <clause> <op> ...
//  <clause> = ~ <clause>
//           = <literal>
//           = ( <expr> )
//      <op> = &
//           = |
// <literal> = <letter> <alnum> ...

class EvalResult {
	bool mBoolResult;
	std::string mError;

public:
	EvalResult() {
		mError = "No result set";
	}

	void setError(const char *format, ...) {
		char	buf[MAX_LINE];
		va_list         ap;
		va_start(ap, format);
		vsprintf(buf, format, ap);
		va_end(ap);
		mError = buf;
	}

	bool isError() const { return ! mError.empty(); }

	std::string getError() const { return mError; }

	void setBool(const bool b) {
		mError = "";
		mBoolResult = b;
	}

	bool getBool() const { return mBoolResult; }
	bool isSatisfied() const { return getBool(); }

	std::string toString() const {
		if (isError()) {
			return mError;
		}

		return boolToString(mBoolResult);
	}
};

static EvalResult eval(const Tokens &tokens, Tokens::const_iterator &, const Literals &frozen, const Literals &thawed);

static EvalResult evalClause(const Tokens &tokens, Tokens::const_iterator &it, const Literals &frozen, const Literals &thawed) {
	EvalResult result;

	switch(it->getType()) {
	case TT_Unknown:
		result.setError("Encountered Unknown token");
		return result;
	case TT_And:
		result.setError("A clause cannot begin with an &");
		return result;
	case TT_Or:
		result.setError("A clause cannot begin with an |");
		return result;
	case TT_Not:
		 {
			 it++;
			 if (it == tokens.end()) {
				result.setError("Expected something after a Not");
				return result;
			 }

			 const EvalResult right = evalClause(tokens, it, frozen, thawed);
			 if (right.isError()) {
				 return right;
			 }

			 result.setBool(! right.getBool());
			 return result;
		 }
	case TT_Literal:
		{
			const std::string name = it->getLiteral();
			Literals::const_iterator litIt = findLiteral(frozen, thawed, name);
			if (litIt  == thawed.end()) {
				result.setError("Unknown literal %s", name.c_str());
				return result;
			}
			result.setBool(litIt->getBool());
			return result;
		}
	case TT_OpenBracket:
		{
			it++;
			if (it == tokens.end()) {
				result.setError("Expected something after an Open Bracket");
				return result;
			}

			const EvalResult right = eval(tokens, it, frozen, thawed);

			it++;
			if (!it->isCloseBracket()) {
				result.setError("Expected Close Bracket");
				return result;
			}

			return right;
		 }
	case TT_CloseBracket:
		result.setError("Unexpected Close Bracket");
		return result;
	case TT_Space:
		result.setError("Unexpected Space");
		return result;
	case TT_Eof:
		result.setError("Unexpected Eof");
		return result;
	}

	result.setError("Interal error in evalClause");
	return result;
}
	
static EvalResult eval(const Tokens &tokens, Tokens::const_iterator &it, const Literals &frozen, const Literals &thawed) {
	EvalResult	result;

	result = evalClause(tokens, it, frozen, thawed);
	it++;
	if (it == tokens.end()) { return result; }

	for (; it != tokens.end(); it++) {
		const TokType type2 = it->getType();
		if (type2 == TT_CloseBracket) {
			it--;	// Unget
			return result;
		}

		if (type2 != TT_And && type2 != TT_Or) {
			result.setError("Unexpected %s -- Only And/Or can connect clauses", it->toString().c_str());
			return result;
		}

		it++;
		if (it == tokens.end()) {
			result.setError("Expected something after an And/Or");
			return result;
		}

		const EvalResult right = evalClause(tokens, it, frozen, thawed);
		if (right.isError()) {
			return right;
		}

		if (type2 == TT_And) {
			result.setBool(result.getBool() && right.getBool());
		}
		else if (type2 == TT_Or) {
			result.setBool(result.getBool() || right.getBool());
		}
	}

	return result;
}

static EvalResult eval(const Tokens &tokens, const Literals &frozen, const Literals &thawed) {
	Tokens::const_iterator it = tokens.begin();
	return eval(tokens, it, frozen, thawed);
}

//-----------------------------------------------------------------------------
// Solve

class SolveResult {
public:
	bool mSatisfied;
	std::string mError;
	Literals mFrozen;
	Literals mThawed;

	SolveResult() {
		mSatisfied = false;
		mError = "Not run yet";
	}

	void setError(const std::string e) {
		mError = e;
	}

	bool isError() const { return ! mError.empty(); }

	void setSatisfied(const Literals &frozen, const Literals &thawed) {
		mSatisfied = true;
		mError = "";
		mFrozen = frozen;
		mThawed = thawed;
	}

	void setUnsat() {
		mSatisfied = false;
		mError = "";
		mFrozen.clear();
		mThawed.clear();
	}

	bool isSatisfied() const { return mSatisfied; }

	std::string toString() const {
		if (isError()) {
			return mError;
		}

		if (!isSatisfied()) {
			return "Unstatisfied";
		}

		return "Satisfied with " + literalsToString(mFrozen, mThawed);
	}
};

static SolveResult solve(const Tokens &tokens, const Literals &frozenLiterals, const Literals &thawedLiterals) {
	SolveResult solveResult;
	const EvalResult evalResult = eval(tokens, frozenLiterals, thawedLiterals);
	if (evalResult.isError()) {
		solveResult.setError(evalResult.getError());
		return solveResult;
	}

	if (evalResult.isSatisfied()) {
		solveResult.setSatisfied(frozenLiterals, thawedLiterals);
		return solveResult;
	}

	if (thawedLiterals.size() == 0) {
		return solveResult;
	}

	Literals frozen = appendFirst(frozenLiterals, thawedLiterals);
	Literals thawed = butFirst(thawedLiterals);

	const int frozenLast = frozen.size() - 1;
	for (int i = 0; i < gnBools; i++) {
		frozen[frozenLast].setBool(gBools[i]);
		SolveResult solveResult = solve(tokens, frozen, thawed);
		if (solveResult.isSatisfied()) {
			return solveResult;
		}
	}

	solveResult.setUnsat();
	return solveResult;
}

static void solve(const Tokens &tokens) {
	const Literals noLiterals;
	const Literals allLiterals = getLiterals(tokens);

	if (allLiterals.size() == 0) {
		std::cerr << "There are no literals -- nothing to solve" << std::endl;
		exit(EXIT_CANNOT_PARSE_INPUT);
	}

	printLiteralsWithoutValues(noLiterals, allLiterals);

	//
	// Eval once to check syntax
	//

	const EvalResult chkResult = eval(tokens, noLiterals, allLiterals);
	if (chkResult.isError()) {
		std::cerr << "Formula has invalid syntax -- " << chkResult.toString() << std::endl;
		exit(EXIT_CANNOT_PARSE_INPUT);
		return;
	}

	//
	// Now solve
	//

	const SolveResult solveResult = solve(tokens, noLiterals, allLiterals);
	std::cout << solveResult.toString() << std::endl;
	
	if (solveResult.isError()) {
		exit(EXIT_CANNOT_PARSE_INPUT);
	}
	else {
		if (solveResult.isSatisfied()) {
			exit(EXIT_SATISFIABLE);
		}
		else {
			exit(EXIT_UNSATISFIABLE);
		}
	}
}

//-----------------------------------------------------------------------------
// Main

static void parseAndSolveLine(const char *line) {
	const Tokens tokens = parseLine(line);
	std::cout << "Parsed input: ";
	printTokens(tokens);
	if (tokens.size() == 0) {
		std::cerr << "No tokens found -- cannot solve";
		return;
	}

	solve(tokens);
}

static void parseAndSolve(FILE *f)
{
	char	line[MAX_LINE+1];

	for (;;)
	{
		if (fgets(line, sizeof(line), f) == NULL) break;
		chomp(line);
		parseAndSolveLine(line);
	}
}

int main(int argc, char *argv[])
{
	if (argc > 1) {
		if (strcmp(argv[1], "-?") == 0) {
			usage();
			return 0;
		}

		char line[MAX_LINE];
		flatten(argc, argv, 1, line, sizeof(line));
		parseAndSolveLine(line);
	}

	parseAndSolve(stdin);
	exit(0);
}
