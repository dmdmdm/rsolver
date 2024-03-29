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

static void usage() {
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
		"~(mike & sally) & ~peter100\n"
		"\n"
		"The following are supported: &=and, |=or, ~=not, ()=brackets, letters=literals\n"
		"There is no attempt at optimization or avoiding recursion\n";
	exit(EXIT_COMMAND_LINE_FAIL);
}

static const long KILO = 1000;
static const long MEGA = KILO * KILO;
static const long GIGA = MEGA * KILO;
static long gnEvals = 0;
static long gnMaxDepth = 0;

//-----------------------------------------------------------------------------
// Bool Util

inline std::string boolToString(const bool b) {
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

	for (;;) {
		if ((c = fgetc(f)) == EOF) break;
		if (c == '\r') continue;
		if (c == '\n') c = ' ';
		contents += c;
	}
	return contents;
}

static std::string prettyNumber(const long n) {
	char	buf[30];

	if (n >= GIGA) {
		snprintf(buf, sizeof(buf), "%ld G", n / GIGA);
		return buf;
	}
	else if (n >= MEGA) {
		snprintf(buf, sizeof(buf), "%ld M", n / MEGA);
		return buf;
	}
	else if (n >= KILO) {
		snprintf(buf, sizeof(buf), "%ld K", n / KILO);
		return buf;
	}

	snprintf(buf, sizeof(buf), "%ld", n);
	return buf;
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
	std::string mLiteral;
	int mLitIndex;

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
	int getLitIndex() const { return mLitIndex; }

	void setLitIndex(const int idx) {
		mLitIndex = idx;
	}

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
	for (auto it = tokens.begin(); it != tokens.end(); it++) {
		if (!out.empty()) out += " ";
		out += it->toString();
	}
	return out;
}

//-----------------------------------------------------------------------------
// Literals Names and Values apart

typedef std::vector<std::string> LitNames;
typedef std::vector<bool> LitValues;

inline int findLitName(const LitNames &litnames, const std::string &target) {
	const int n = (int)litnames.size();
	for (int i = 0; i < n; i++) {
		if (strcmp(litnames[i].c_str(), target.c_str()) == 0) return i;
	}
	return -1;
}

static void printLitNames(const LitNames &names) {
	std::string out;

	for (auto it = names.begin(); it != names.end(); it++) {
		if (!out.empty()) out += " ";
		out += *it;
	}

	std::cout << "Unique Literals: " << out << std::endl;
}

static void getLitNames(const Tokens &tokens, LitNames &litnames) {
	for (auto it = tokens.begin(); it != tokens.end(); it++) {
		if (it->isLiteral()) {
			if (findLitName(litnames, it->getLiteral()) >= 0) continue;
			litnames.push_back(it->getLiteral());
		}
	}
}

static void assignLiteralIndexes(Tokens &tokens, const LitNames &litnames) {
	for (auto it = tokens.begin(); it != tokens.end(); it++) {
		if (it->isLiteral()) {
			const int idx = findLitName(litnames, it->getLiteral());
			if (idx < 0) continue;
			it->setLitIndex(idx);
		}
	}
}

//-----------------------------------------------------------------------------
// Working Literal Values

class WorkingValues {
	const LitNames	*mpNames; // This is a pointer so we don't copy all the names when we are cloned
	LitValues	mValues;
	int			mStartOfThawed;

	void initValues() {
		mValues.clear();
		if (mpNames == nullptr) return;
		const int n = (int)mpNames->size();
		for (int i = 0; i < n; i++) {
			mValues.push_back(false);
		}
	}

	void initStartOfThawed() {
		if (mValues.size() > 0) {
			mStartOfThawed = 0;
		}
		else {
			mStartOfThawed = -1;
		}
	}

public:
	WorkingValues() {
		mpNames = nullptr;
		mValues.clear();
		initStartOfThawed();
	}

	WorkingValues(const LitNames *pNames) {
		mpNames = pNames;
		initValues();
		initStartOfThawed();
	}

	WorkingValues(const WorkingValues &other) {
		mpNames = other.mpNames;
		mValues = other.mValues;
		mStartOfThawed = other.mStartOfThawed;
	}

	void clear() {
		mpNames = nullptr;
		initValues();
		initStartOfThawed();
	}

	void advance(const WorkingValues &in) {
		mpNames = in.mpNames;
		mValues = in.mValues;
		mStartOfThawed = in.mStartOfThawed + 1;
	}

	size_t sizeThawed() const {
		if (mValues.empty() || mStartOfThawed < 0) return 0;
		return mValues.size() - mStartOfThawed;
	}

	void setFrozenLastBool(const bool b) {
		mValues[mStartOfThawed - 1] = b;
	}

	bool getBool(const int i) const {
		return mValues[i];
	}

	std::string toString() const {
		std::string out;

		if (mpNames == nullptr) {
			return "mpNames is null";
		}

		const int n = (int)mpNames->size();
		for (int i = 0; i < n; i++) {
			if (!out.empty()) out += " ";
			out += mpNames->at(i) + "=" + boolToString(mValues[i]);
		}
		return out;
	}

	bool operator==(const WorkingValues &other) const {
		return mValues == other.mValues;
	}
};

//-----------------------------------------------------------------------------
// Eval
//    <expr> = <clause> <op> <clause> <op> ...
//           = <clause>
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

//-----------------------------------------------------------------------------
// Eval

static EvalResult eval(const Tokens &, Tokens::const_iterator &, const WorkingValues &, const int depth);

static EvalResult evalClause(const Tokens &tokens, Tokens::const_iterator &it, const WorkingValues &literals, const int depth) {
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

			const EvalResult right = evalClause(tokens, it, literals, depth + 1);
			if (right.isError()) {
				return right;
			}

			result.setBool(! right.getBool());
			return result;
		 }
	case TT_Literal:
		{
			const int idx = it->getLitIndex();
			if (idx < 0) {
				result.setError("Unknown Literal %s", it->getLiteral().c_str());
				return result;
			}
			result.setBool(literals.getBool(idx));
			return result;
		}
	case TT_OpenBracket:
		{
			it++;
			if (it == tokens.end()) {
				result.setError("Expected something after an Open Bracket");
				return result;
			}

			const EvalResult right = eval(tokens, it, literals, depth + 1);

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

static EvalResult eval(const Tokens &tokens, Tokens::const_iterator &it, const WorkingValues &literals, const int depth) {
	if (depth > gnMaxDepth) {
		gnMaxDepth = depth;
	}

	EvalResult result = evalClause(tokens, it, literals, depth + 1);
	it++;
	if (it == tokens.end()) {
		return result;
	}

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

		const EvalResult right = evalClause(tokens, it, literals, depth + 1);
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

static EvalResult evalMain(const Tokens &tokens, const WorkingValues &literals, const int depth) {
	gnEvals++;

	if ((gnEvals % MEGA) == 0) {
		std::cerr << "Evals: " << prettyNumber(gnEvals) << std::endl;
	}

	auto it = tokens.begin();
	return eval(tokens, it, literals, depth + 1);
}

//-----------------------------------------------------------------------------
// Solve

class SolveResult {
public:
	bool mSatisfied;
	std::string mError;
	WorkingValues mLiterals;

	SolveResult() {
		mSatisfied = false;
		mError = "Not run yet";
	}

	void setError(const std::string &error) {
		mError = error;
	}

	bool isError() const { return ! mError.empty(); }

	void setSatisfied(const WorkingValues &literals) {
		mSatisfied = true;
		mError.clear();
		mLiterals = literals;
	}

	void setUnsat() {
		mSatisfied = false;
		mError.clear();
		mLiterals.clear();
	}

	bool isSatisfied() const { return mSatisfied; }

	std::string toString() const {
		if (isError()) {
			return mError;
		}

		if (!isSatisfied()) {
			return "Unstatisfied";
		}

		return "Satisfied with " + mLiterals.toString();
	}
};

static SolveResult solve(const Tokens &tokens, const WorkingValues &literals, const int depth) {
	SolveResult solveResult;

	const EvalResult evalResult = evalMain(tokens, literals, depth + 1);
	if (evalResult.isError()) {
		solveResult.setError(evalResult.getError());
		return solveResult;
	}

	if (evalResult.isSatisfied()) {
		solveResult.setSatisfied(literals);
		return solveResult;
	}

	if (literals.sizeThawed() == 0) {
		return solveResult;
	}

	WorkingValues litNew;
   	litNew.advance(literals);

	for (int i = 0; i < gnBools; i++) {
		litNew.setFrozenLastBool(gBools[i]);
		const SolveResult solveResult = solve(tokens, litNew, depth + 1);
		if (solveResult.isSatisfied()) {
			return solveResult;
		}
	}

	solveResult.setUnsat();
	return solveResult;
}

static void solveMain(Tokens &tokens) {
	const int depth = 0;
	LitNames litnames;
	getLitNames(tokens, litnames);
	assignLiteralIndexes(tokens, litnames);

	if (litnames.size() == 0) {
		std::cerr << "There are no literals -- nothing to solve" << std::endl;
		exit(EXIT_CANNOT_PARSE_INPUT);
	}

	printLitNames(litnames);

	//
	// Eval once to check syntax
	//

	WorkingValues literals(&litnames);
	const EvalResult chkResult = evalMain(tokens, literals, depth + 1);
	if (chkResult.isError()) {
		std::cerr << "Formula has invalid syntax -- " << chkResult.toString() << std::endl;
		exit(EXIT_CANNOT_PARSE_INPUT);
		return;
	}

	//
	// Now solve
	//

	const SolveResult solveResult = solve(tokens, literals, depth + 1);
	std::cout << solveResult.toString() << std::endl;
	std::cout << "  Number of Evals: " << prettyNumber(gnEvals) << std::endl;
	std::cout << "        Max Depth: " << prettyNumber(gnMaxDepth) << std::endl;
	
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

	Tokens tokens = parseLine(line);
	if (tokens.size() == 0) {
		std::cerr << "No tokens found -- cannot solve";
		return;
	}

	std::cout << "Parsed Input: " << tokensToString(tokens) << std::endl;
	solveMain(tokens);
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
