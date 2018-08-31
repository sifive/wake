#include "symbol.h"

int main(int argc, const char **argv) {
  for (int i = 1; i < argc; ++i) {
    Lexer lex(argv[i]);
    Symbol x;
    do {
      x = lex.get();
      switch (x.type) {
        case ID:       printf("ID ");       fwrite(x.start, 1, (x.end - x.start), stdout); printf("\n"); break;
        case OPERATOR: printf("OPERATOR\n"); break;
        case STRING:   printf("STRING\n");   break;
        case DEF:      printf("DEF\n");      break;
        case LAMBDA:   printf("LAMBDA\n");   break;
        case EQUALS:   printf("EQUALS\n");   break;
        case POPEN:    printf("POPEN\n");    break;
        case PCLOSE:   printf("PCLOSE\n");   break;
        case EOL:      printf("EOL\n");      break;
        case INDENT:   printf("INDENT\n");   break;
        case DEDENT:   printf("DEDENT\n");   break;
        case ERROR:    printf("ERROR\n");    break;
        case END:      printf("END\n");      break;
      }
    } while (x.type != ERROR && x.type != END);
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
