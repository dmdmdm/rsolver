# rsolver
A toy SAT (boolean SATisfiability) solver
https://en.wikipedia.org/wiki/Satisfiability

# Usage
You can put the logic expression on the command line (in quotes) or send it via stdin
eg:
  
    ./rsolver 'a & b'
    
    echo 'a & b' | ./rsolver
    
# Output
It will say either "Unsatisfied" or "Satisfied with a=True b=True" (or whatever literals work)
     
# Example Expressions
    a & ~b
    x & ~x
    mike & sally | ~peter
    ~(mike & sally) | ~peter100
       
# Grammar
       <expr> = <clause> <op> <clause> <op> ...
              = <clause>
     <clause> = ~ <clause>
              = <literal>
              = ( <expr> )
         <op> = &
              = |
    <literal> = <letter> <alnum> ...

# Warning
There is no attempt at optimization or avoiding recursion.
So for complex input it will be slow or possibly stack overflow.
Complexity is O(2^n) for n literals.
