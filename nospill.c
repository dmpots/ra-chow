int main(int argc, char **argv) {

    int result = 0;
    int counter = argc;

    while(counter > 0) {
        if(counter % 4 == 0) {
            result--;
            break;
        } else {
            result++;
        }
        counter = counter - 1;
    }
    printf("Result = %d\n", result);
}
