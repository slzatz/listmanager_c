
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>

#define COLUMN 20

int main()
{
    //char line[50];
    char preamble[] = "We the People of the United States, in Order to form a more perfect Union, establishJustice,insuredomesticTranquility,provideforthecommondefense, promote the general Welfare, and secure the Blessings of Liberty to ourselves and our Posterity, do ordain and establish this Constitution for the United States of America.";
    char *base,*right_margin;
    int length,width;

    length = strlen(preamble); //not fixed -- this is decremented as each line is created
    base = preamble; //char * to string that is going to be wrapped ? better named remainder?
    width = COLUMN; //wrapping width

    while(*base) //exit when hit the end of the string '\0' - #1
    {
        if(length <= width) //after creating whatever number of lines if remainer <= width: get out
        {
            puts(base);     /* display string */
            return(0);      /* and leave */
        }
        right_margin = base+width - 1; //each time base pointer moves you are adding the width to it and checking for spaces
        while(!isspace(*right_margin)) //#2
        {
            right_margin--;
            if( right_margin == base) //definitely not getting this line feels like happens if no space in line to stay under width
            {
                right_margin += width - 1;
                break; /////
            }    /////

        } //end #2
        //strncpy(line, base, right_margin - base);
        char * line = malloc(right_margin - base + 2);
        memcpy(line, base, right_margin - base + 1);
        line[right_margin - base + 1] = '\0';
        if (isspace(line[right_margin - base])) line[right_margin - base] = '#';
        //line[right_margin - base] = '#';
        printf("%s -> %d\n", line, strlen(line));
        free(line);
        //*right_margin = '\0'; //feels like we're consuming characters but not printing the space
        //puts(base);

        // couldn't you just adjust base and then length == strlen(base) ?
        length -= right_margin-base+1;      /* +1 for the space */
        //base = right_margin+1; //move the base pointer to the beginning of what will be the next line
        base = right_margin + 1; //move the base pointer to the beginning of what will be the next line
    } //end #1

    return(0);
}
