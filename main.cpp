#include "symbol.h"

int main(int argc, const char **argv) {
  for (int i = 1; i < argc; ++i) {
    Lexer lex(argv[i]);
    while (lex.next.type != ERROR && lex.next.type != END) {
      switch (lex.next.type) {
        case ID:  printf("ID "); fwrite(lex.next.start, 1, (lex.next.end - lex.next.start), stdout); printf("\n"); break;
        default: printf("%s\n", symbolTable[lex.next.type]); break;
      }
      lex.consume();
    } 
  }

  return 0;
/*
  ActionQueue queue;
  while (!queue.empty()) {
    Action *doit = queue.front();
    queue.pop_front();
    doit->execute(queue);
  }
*/
}
