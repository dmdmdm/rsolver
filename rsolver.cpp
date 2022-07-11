// rsolver
// (c) Copyright 2022, Recursive Pizza

#include <stdio.h>
#include <stdarg.h>
#include <string>
#include <string.h>
#include <vector>
#include <iostream>

// Same exit codes as minisat
// (Except we use 0 for EXIT_SATISFIABLE and they use 10)
enum { EXIT_COMMAND_LINE_FAIL = 0, EXIT_CANNOT_READ_INPUT = 1, EXIT_CANNOT_PARSE_INPUT = 3, EXIT_SATISFIABLE = 0, EXIT_SATISFIABLE_MINISAT = 10, EXIT_UNSATISFIABLE = 20 };

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

static int gnEvals = 0;
static int gnLookUps = 0;

//-----------------------------------------------------------------------------
// Bool Util

static std::string boolToString(const bool b) {
	return b ? "True" : "False";
}

static bool gBools[] = { true, false };
static int gnBools = sizeof(gBools) / sizeof(gBools[0]);

//-----------------------------------------------------------------------------
// String Util

enum { MAX_ERROR = 5 * 1024 };

static std::string flatten(const int argc, char *argv[], const int optind) {
	std::string out;
	for (int i = optind; i < argc; i++) {
		if (!out.empty()) out += " ";
		out += argv[i];
	}
	return out;
}

static std::string readFile(FILE *f) {
	std::string contents;
	int c;

	for (;;)
	{
		if ((c = fgetc(f)) == EOF) break;
		if (c == '\r') continue;
		if (c == '\n') c = ' ';
		contents += c;
	}
	return contents;
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
	friend class Tokenizer;

public:
	std::string mLiteral;	// Public for speed

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
	std::string mLine;
	const char *mpPosition;

public:
	Tokenizer(const std::string &line) {
		mLine = line;
		mpPosition = mLine.c_str();
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

static Tokens parseLine(const std::string &line) {
	Tokenizer tokenizer(line);
	Tokens tokens;

	for (;;) {
		const Token tok = tokenizer.getToken();
		if (tok.isEof()) break;
		tokens.push_back(tok);
	}

	return tokens;
}

static std::string tokensToString(const Tokens &tokens) {
	std::string out;
	for (Tokens::const_iterator it = tokens.begin(); it != tokens.end(); it++) {
		if (!out.empty()) out += " ";
		out += it->toString();
	}
	return out;
}

//-----------------------------------------------------------------------------
// Literals

class Literal {
	std::string mName;
	bool mValue;
public:

	Literal(const std::string &name) {
		mName = name;
		mValue = false;
	}

	bool isMatch(const std::string &name) const {
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
	std::cout << "Unique Literals: " << literalsWithoutValues(frozen, thawed) << std::endl;
}

// Of course a std::map<> would be faster but we need to maintain the order
static Literals::const_iterator findLiteral(const Literals &frozen, const Literals &thawed, const std::string &target) {
	gnLookUps++;

	for (Literals::const_iterator it = frozen.begin(); it != frozen.end(); it++) {
		if (it->isMatch(target)) return it;
	}

	for (Literals::const_iterator it = thawed.begin(); it != thawed.end(); it++) {
		if (it->isMatch(target)) return it;
	}
	return thawed.end();
}

static Literals getLiterals(const Tokens &tokens) {
	Literals noLiterals;
	Literals allLiterals;

	for (Tokens::const_iterator it = tokens.begin(); it != tokens.end(); it++) {
		if (it->isLiteral()) {
			if (findLiteral(noLiterals, allLiterals, it->mLiteral) != allLiterals.end()) continue;
			Literal lit(it->mLiteral);
			allLiterals.push_back(lit);
		}
	}
	return allLiterals;
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
		char	buf[MAX_ERROR];
		va_list         ap;
		va_start(ap, format);
		vsprintf(buf, format, ap);
		va_end(ap);
		mError = buf;
	}

	bool isError() const { return ! mError.empty(); }

	std::string getError() const { return mError; }

	void setBool(const bool b) {
		mError.clear();
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
			const Literals::const_iterator litIt = findLiteral(frozen, thawed, it->mLiteral);
			if (litIt == thawed.end()) {
				result.setError("Unknown Literal %s", it->mLiteral.c_str());
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
	gnEvals++;

	EvalResult result = evalClause(tokens, it, frozen, thawed);
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

	void setError(const std::string &error) {
		mError = error;
	}

	bool isError() const { return ! mError.empty(); }

	void setSatisfied(const Literals &frozen, const Literals &thawed) {
		mSatisfied = true;
		mError.clear();
		mFrozen = frozen;
		mThawed = thawed;
	}

	void setUnsat() {
		mSatisfied = false;
		mError.clear();
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

	Literals frozenNew = appendFirst(frozenLiterals, thawedLiterals);
	const Literals thawedNew = butFirst(thawedLiterals);

	const int frozenNewLast = frozenNew.size() - 1;
	for (int i = 0; i < gnBools; i++) {
		frozenNew[frozenNewLast].setBool(gBools[i]);
		SolveResult solveResult = solve(tokens, frozenNew, thawedNew);
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
	std::cout << "Number of Evals: " << gnEvals << std::endl;
	std::cout << "Number of Lookups: " << gnLookUps << std::endl;
	
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

static void parseAndSolveLine(const std::string &line) {
	if (line.empty()) {
		std::cerr << "Contents is empty -- cannot solve";
		return;
	}

	const Tokens tokens = parseLine(line);
	if (tokens.size() == 0) {
		std::cerr << "No tokens found -- cannot solve";
		return;
	}

	std::cout << "Parsed Input: " << tokensToString(tokens) << std::endl;
	solve(tokens);
}

static void parseAndSolveFile(FILE *f) {
	parseAndSolveLine(readFile(f));
}

int main(int argc, char *argv[]) {
	if (argc > 1) {
		if (strcmp(argv[1], "-?") == 0) {
			usage();
			return 0;
		}

		parseAndSolveLine(flatten(argc, argv, 1));
	}

	parseAndSolveFile(stdin);
	exit(0);
}
