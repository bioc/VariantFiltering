/****************************************************
 *        C-methods for the WeightMatrix class      *
 *                Author: Robert Castelo            *
 *                robert.castelo@upf.edu            *
 *        Copyright (c) 2013, Robert Castelo        *
 *      Artistic License 2.0. See following URL     *
 *  http://www.r-project.org/Licenses/Artistic-2.0  *
 ****************************************************/

#include <ctype.h>
#include <Biostrings_interface.h>
#include "VariantFiltering.h"

/* constants */
 
#define MAXVARS 50              /* maximum number of variables */
#define MAXVALS 10              /* maximum size of the domain of a variable */
#define MAXWIDZ 256             /* maximum width of a value of a domain */
#define MAXSTAK MAXVARS*MAXVARS*MAXVALS /* maximum size of the stack used to explore the prefix tree */



/* data types definitions */

typedef struct node {
  struct node* next[MAXVALS];   /* pointer to a next node in the tree */
  int          idep;            /* index dependence */
  int          depo;            /* dependence order */
  double*      weights;         /* weights, set when the node is a leaf */
} Tree;

typedef struct {
  int  nvars;
  int  nvals[MAXVARS];
  char vars[MAXVARS][MAXWIDZ];
  char vals[MAXVARS][MAXVALS][MAXWIDZ];
  int  deps[MAXVARS][MAXVARS];
  int  depsmap[MAXVARS][MAXVARS];
  int  ndeps[MAXVARS];
  Tree w[MAXVARS];
} WeightMatrix;



/* prototypes of private functions */

int wm_ivar(WeightMatrix*, char*);
int wm_ival(WeightMatrix*, int, char*);
Rboolean read_wm(FILE*, WeightMatrix* wm, char* errormsg);
void show_wm(WeightMatrix*);
static void destroy_wm(SEXP);
double wm_score(WeightMatrix*, const char*);

/*
 * Store, manage and free WeightMatrix objects using external pointers as described
 * by Martin Morgan here http://stackoverflow.com/questions/7032617/storing-c-objects-in-r
*/

/* entry point to read_wm */
SEXP
scoss_read_wm(SEXP fnameR) {
  const char* fname = CHAR(STRING_ELT(fnameR, 0));
  SEXP wmR;
  FILE* fdwm;
  WeightMatrix* wm = Calloc(1, WeightMatrix);
  char errormsg[4096];
  Rboolean errorflag;

  if (!(fdwm=fopen(fname, "rt"))) {
    error("impossible to open %s\n", fname);
  }

  PROTECT(wmR = R_MakeExternalPtr(wm, R_NilValue, R_NilValue));
  R_RegisterCFinalizerEx(wmR, destroy_wm, TRUE);
  errorflag = read_wm(fdwm, (WeightMatrix *) R_ExternalPtrAddr(wmR), errormsg);
  UNPROTECT(1); /* wmR */

  fclose(fdwm);

  if (errorflag) {
    destroy_wm(wmR);
    error(errormsg);
  }

  return(wmR);
}

/* entry point to wm_score */
SEXP
scoss_wm_score(SEXP wmR, SEXP dnastringR, SEXP nscoR) {
  int i, j, k;
  int ndnastrings = length(dnastringR);
  WeightMatrix* wm = (WeightMatrix *) R_ExternalPtrAddr(wmR);
  int nsco = INTEGER(nscoR)[0];
  double* scores;
  SEXP scoresR;

  PROTECT(scoresR = allocVector(REALSXP, nsco));
  scores = REAL(scoresR);
  k = 0;
  for (i=0; i < ndnastrings; i++) {
    const char* dnaseq = CHAR(STRING_ELT(dnastringR, i));

    for (j=0; j < strlen(dnaseq)-wm->nvars+1; j++) {
      char buf[MAXVARS];

      strncpy(buf, dnaseq+j, wm->nvars);
      buf[wm->nvars] = 0;
      scores[k++] = wm_score(wm, buf);
    }
  }
  UNPROTECT(1); /* scoresR */

  return(scoresR);
}


/* entry point to wm_score_DNAStringSet */
SEXP
scoss_wm_score_DNAStringSet(SEXP wmR, SEXP dnaStringSet, SEXP nscoR) {
  /* cachedXStringSet S; */
  XStringSet_holder S;
  int S_length, i, j, k;
  int nsco = INTEGER(nscoR)[0];
  WeightMatrix* wm = (WeightMatrix *) R_ExternalPtrAddr(wmR);
  double* scores;
  SEXP scoresR;

  /* S = cache_XStringSet(dnaStringSet); */
  S = hold_XStringSet(dnaStringSet);
  S_length = get_XStringSet_length(dnaStringSet);

  PROTECT(scoresR = allocVector(REALSXP, nsco));
  scores = REAL(scoresR);
  i = 0;
  for (j=0; j < S_length; j++) {
    /* cachedCharSeq S_elt; */
    Chars_holder S_elt;
    char buf[MAXVARS];
    int k;

    /* S_elt = get_cachedXStringSet_elt(&S, j); */
    S_elt = get_elt_from_XStringSet_holder(&S, j);
    for (k=0; k < S_elt.length; k++) {
      const char* c = S_elt.seq+k;

      buf[k] = DNAdecode(*c);
    }
    buf[k] = 0;

    for (k=0; k < S_elt.length-wm->nvars+1; k++) {  
      char buf2[MAXVARS];

      strncpy(buf2, buf+k, wm->nvars);
      buf2[wm->nvars] = 0;
      scores[i++] = wm_score(wm, buf2);
    }

  }
  UNPROTECT(1); /* scoresR */

  return(scoresR);
}

/* entry point to wm_show_wm */
void
scoss_show_wm(SEXP wmR) {
  WeightMatrix* wm = (WeightMatrix *) R_ExternalPtrAddr(wmR);

  show_wm(wm);
}

/* entry point to wm_width_wm */
SEXP
scoss_width_wm(SEXP wmR) {
  WeightMatrix* wm = (WeightMatrix *) R_ExternalPtrAddr(wmR);
  SEXP wR;

  PROTECT(wR = allocVector(INTSXP, 1));
  INTEGER(wR)[0] = wm->nvars;
  UNPROTECT(1); /* wR */

  return wR;
}

/* entry point to wm_conserved_positions_wm */
SEXP
scoss_conserved_positions_wm(SEXP wmR) {
  WeightMatrix* wm = (WeightMatrix *) R_ExternalPtrAddr(wmR);
  SEXP cpR;
  int i, j, ncp;

  ncp=0;
  for (i=0; i < wm->nvars; i++)
    if (wm->nvals[i] == 1)
      ncp++;

  PROTECT(cpR = allocVector(INTSXP, ncp));
  j=0;
  for (i=0; i < wm->nvars; i++)
    if (wm->nvals[i] == 1)
      INTEGER(cpR)[j++] = i+1;

  UNPROTECT(1); /* wR */

  return cpR;
}

/*
  FUNCTION: read_wm
  PURPOSE: read a weight matrix from a file and store it into the corresponding data structure
  PARAMETERS: fd - file descriptor of the file containing the weight matrix
              wm - pointer to a WeightMatrix data structure where the matrix should be stored
              errormsg - text of the error message, if any
  RETURN: TRUE if there was an error; FALSE otherwise
*/
  
Rboolean
read_wm(FILE* fd, WeightMatrix* wm, char* errormsg) {
  char ptoken[256];
  int  line=1;
  int  deflineread=0;
  int  i;
  long wmbegin=0;

  /* read definition line with all variables present in the WM and their domains */

  wm->nvars=999;
  for (i=0; i < MAXVARS; i++) {
    int j;

    wm->ndeps[i] = 0;
    wm->w[i].weights = NULL;
    for (j=0; j < MAXVALS; j++)
      wm->w[i].next[j] = NULL;
  }

  i = 0;

  ptoken[0]=0;
  while (!feof(fd) && i < wm->nvars) {
    char buf[4096];

    if (fgets(buf,4096,fd)) {
      char  token[256];
      char* p = buf;
      int   n;
      int   ntokens;

      ntokens = sscanf(p," %[a-zA-Z0-9_]%n",token,&n);
      p += n;

      if (!deflineread && ntokens == 1) {  /* definition line */
        char* q;

        q=token;
        while ((*q=tolower(*q))) q++;      /* change keyword 'WM' to lowercase */

        wm->nvars=0;
        if (!strcmp(token,"wm")) {
          int nv;

          while (sscanf(p," %[a-zA-Z0-9_] %d%n",token,&nv,&n) == 2) {
            strcpy(wm->vars[wm->nvars],token);
            wm->nvals[wm->nvars] = nv;
            wm->nvars++;

            p += n;
          }

          wmbegin=ftell(fd);
          deflineread = 1;
        } else {
          strcpy(errormsg, "definition line in the weight matrix file should start with the word WM\n");
          return(TRUE);
        }

      } else if (deflineread && strcmp(token,ptoken)) {  /* new variable */

        char val[MAXWIDZ];
        int  nv = 0;
        int  ivar=wm_ivar(wm,token);

        /* read variable domain */
        while (sscanf(p," %[a-zA-Z0-9_]%n",val,&n) == 1 && nv < wm->nvals[ivar]) {
          strcpy(wm->vals[ivar][nv],val);
          nv++;
          p += n;
        }

        /* read variable dependencies */
        wm->ndeps[ivar] = 0;
        while (sscanf(p," %[a-zA-Z0-9_]%n",val,&n) == 1) {
          wm->deps[ivar][wm->ndeps[ivar]] = wm_ivar(wm,val);
          wm->ndeps[ivar]++;

          p += n;
        }

        wm->w[ivar].idep = -1;
        if (wm->ndeps[ivar] > 0) {
          for (nv=0;nv<wm->nvals[wm->deps[ivar][0]];nv++)
            wm->w[ivar].next[nv] = NULL;
          wm->w[ivar].idep = wm->deps[ivar][0];
          wm->w[ivar].depo = 0;
        }

        i++;
      }

      strcpy(ptoken,token);

      line++;
    }

  }

  if (!deflineread) {
    strcpy(errormsg, "no definition line found in the weight matrix file\n");
    return(TRUE);
  }

  fseek(fd,wmbegin,SEEK_SET);
  line = 1;

  /* read now the weight matrix */

  ptoken[0]=0;
  while (!feof(fd)) {
    char buf[4096];

    if (fgets(buf,4096,fd)) {
      char  token[MAXWIDZ];
      char* p = buf;
      int   n;
      int   ivar;

      if (sscanf(p," %[a-zA-Z0-9_]%n",token,&n) == 1) {
        p += n;

        ivar=wm_ivar(wm,token);

        if (!strcmp(token,ptoken)) {                /* weights */
          char   val[MAXWIDZ];
          double w[MAXVALS];
          int    nv = 0;
          Tree*  ptreep;

          /* read weights */
          while (sscanf(p," %lf%n",&w[nv],&n) == 1 && nv < wm->nvals[ivar]) {
            nv++;
            p += n;
          }

          ptreep = &wm->w[ivar];
          nv = 0;

          /* read values where weights depend on */
          while (sscanf(p," %[a-zA-Z0-9_]%n",val,&n) == 1 && nv < wm->ndeps[ivar]) {
            int iv = wm_ival(wm,wm->deps[ivar][nv],val);

            if (!ptreep->next[iv]) {
              int k;

              ptreep->next[iv] = Calloc(1, Tree);

              for (k=0;k<wm->nvals[wm->deps[ivar][nv]];k++)
                ptreep->next[iv]->next[k] = NULL;
              ptreep->next[iv]->idep = -1;
              ptreep->next[iv]->depo = wm->ndeps[ivar] - 1;
            }

            ptreep->idep = wm->deps[ivar][nv];
            ptreep->depo = nv;
            ptreep = ptreep->next[iv];

            nv++;
            p += n;
          }

          /* finally set weights in the right leaf of the tree */

          ptreep->weights = Calloc(wm->nvals[ivar], double);
          for (nv=0;nv<wm->nvals[ivar];nv++)
            ptreep->weights[nv] = w[nv];

        }

        strcpy(ptoken,token);
      }

      line++;

    }
  }

  return(FALSE);
}



/*
  FUNCTION: show_wm
  PURPOSE: print the contents of the weight matrix to stdout
  PARAMETERS: wm - a WeightMatrix C data structure
  RETURN: none
*/

void
show_wm(WeightMatrix* wm) {
  int i;

  Rprintf("Weight matrix on %d positions\n\n", wm->nvars);

  for (i=0;i<wm->nvars;i++) {
    int   j;
    int   k;
    int   vals[MAXVALS];
    Tree* stack[MAXSTAK];
    int   vstack[MAXSTAK];
    int   nstack, nvstack, nlines, totallines;

    Rprintf("%s\t",wm->vars[i]);

    for (j=0;j<wm->nvals[i];j++)
      Rprintf("%s\t",wm->vals[i][j]);

    totallines = 1;
    for (j=0;j<wm->ndeps[i];j++) {
      Rprintf("%s\t",wm->vars[wm->deps[i][j]]);
      totallines = totallines * wm->nvals[wm->deps[i][j]];
    }
    Rprintf("\n");

    if (wm->ndeps[i] > 0) {
      nstack = 0;
      nvstack = 0;
      nlines = 0;

      for (j=0;j<wm->nvals[wm->deps[i][0]];j++) {
        vstack[nvstack] = j;
        nvstack++;

        if (nstack >= MAXSTAK)
          error("show_wm: stack overflow in exploring the prefix tree of a WeightMatrix\n");

        stack[nstack] = wm->w[i].next[j];
        nstack++;
      }

      while (nstack > 0) {
        Tree* p;

        p = stack[nstack-1];
        nstack--;

        if (p->idep > -1) {

          k = p->depo-1;
          vals[k] = vstack[nvstack-1];
          nvstack--;

          for (j=0;j<wm->nvals[p->idep];j++) {
            if (nstack >= MAXSTAK)
              error("show_wm: stack overflow in exploring the prefix tree of a WeightMatrix object\n");

            stack[nstack] = p->next[j];
            if (!stack[nstack]) {
              int q;
              Rprintf("pushing NULL pointer!!! %d %d %d %d %d\n",i,j,p->idep,p->depo,wm->nvals[p->idep]);
              for (q=0;q<p->depo;q++)
                Rprintf("%s ",wm->vals[wm->deps[i][q]][vals[q]]);
              Rprintf("\n");
            }
            nstack++;

            vstack[nvstack] = j;
            nvstack++;
          }

        } else {

          k = p->depo;
          vals[k] = vstack[nvstack-1];
          nvstack--;

          if (totallines < 7 || nlines < 3 || totallines - nlines < 4) {
            Rprintf("%s",wm->vars[i]);

            for (j=0;j<wm->nvals[i];j++)
              Rprintf("\t%.3f",p->weights[j]);

            k = p->depo+1;
            for (j=0;j<k;j++)
              Rprintf("\t%s",wm->vals[wm->deps[i][j]][vals[j]]);

            Rprintf("\n");
          } else if (nlines == 3)
            Rprintf("[... %d more lines ...]\n", totallines - 6);
          nlines++;
        }

      }

    } else {

      Rprintf("%s",wm->vars[i]);

      for (j=0;j<wm->nvals[i];j++)
        Rprintf("\t%.3f",wm->w[i].weights[j]);
      Rprintf("\n");

    }

  }

}



/*
  FUNCTION: destroy_wm
  PURPOSE: free the memory allocated in the weight matrix when it was created
  PARAMETERS: wm - a WeightMatrix S4 object
  RETURN: none
*/

static void
destroy_wm(SEXP wmR) {
  int i;
  WeightMatrix* wm;

  if (R_ExternalPtrAddr(wmR) == NULL)
    return;

  wm = (WeightMatrix *) R_ExternalPtrAddr(wmR);

  for (i=0;i<wm->nvars;i++) {
    int   j;
    Tree* stack[MAXSTAK];
    int   nstack;

    memset(stack, 0, MAXSTAK * sizeof(Tree*));
    stack[0] = &(wm->w[i]);

    if (wm->ndeps[i] > 0) {
      nstack = 0;

      for (j=0;j<wm->nvals[wm->deps[i][0]] && stack[nstack];j++) {
        if (nstack >= MAXSTAK)
          error("wmscore: stack overflow in exploring the prefix tree of a wm. Possibly, not all memory allocated by the WeightMatrix object has been properly freed.\n");

        stack[nstack] = wm->w[i].next[j];
        nstack++;
      }

      while (nstack > 0) {
        Tree* p;

        p = stack[nstack-1];
        nstack--;

        if (p->idep > -1) {

          for (j=0;j<wm->nvals[p->idep];j++) {
            if (nstack >= MAXSTAK)
              error("wmscore: stack overflow in exploring the prefix tree of a wm. Possibly, not all memory allocated by the WeightMatrix object has been properly freed.\n");

            stack[nstack] = p->next[j];
            nstack++;
          }

          if (p)
            Free(p);

       } else if (p->weights)
          Free(p->weights);
      }

    } else if (wm->w[i].weights)
      Free(wm->w[i].weights);

  }

  Free(wm);
  R_ClearExternalPtr(wmR);
}



/*
  FUNCTION: wm_score
  PURPOSE: use the weight matrix to score character strings
  PARAMETERS: wm - a WeightMatrix C data structure
              record - the character string to score
  RETURN: the score
*/

double
wm_score(WeightMatrix* wm, const char* record) {
  char   recordc[4096];
  const char*  p;
  char   values[MAXVARS][MAXWIDZ];
  int    ncommas=0;
  int    i=0;
  double score;

  p=record;
  while (*p) {
    if (*p == ',')
      ncommas++;
    p++;
  }

  if (ncommas > 0 && ncommas != wm->nvars-1)
    error("input weight matrix has %d positions while vector %s has %d\n", wm->nvars, record, ncommas+1);

  if (ncommas == 0 && wm->nvars > 1) {

    p=record;
    sprintf(recordc,"%c",*p++);
    while (*p) {
      char buf[MAXWIDZ];

      sprintf(buf,",%c",*p++);
      strcat(recordc,buf);
    }

  } else
    strcpy(recordc,record);

  p=recordc;
  while (*p) {
    char val[MAXWIDZ];
    int  n;

    if (sscanf(p," %[a-zA-Z0-9_]%n",val,&n) == 1) {
      char* q;

      q=val;
      while ((*q=tolower(*q))) q++;  /* put value in lowercase */
      strcpy(values[i++],val);
      p+=n;
    } else
      p++;
  }

  if (i != wm->nvars)
    error("input weight matrix has %d positions while vector %s has %d\n", wm->nvars, record, i);

  score = 0.0;
  i = 0;
  while (i < wm->nvars && !ISNA(score)) {
    Tree* ptreep;
    int iv = 0;

    ptreep = &(wm->w[i]);

    if (wm->ndeps[i] > 0)
      while (ptreep->idep > -1 && iv > -1) {
        iv = wm_ival(wm,ptreep->idep,values[ptreep->idep]);
        if (iv > -1)
          ptreep = ptreep->next[iv];
      }

    if (iv > -1) {
      iv = wm_ival(wm, i, values[i]);
      if (iv > -1)
        score += ptreep->weights[iv];
      else
        score = NA_REAL;
    } else
      score = NA_REAL;

    i++;
  }

  return score;
}



/*
  FUNCTION: wm_ivar
  PURPOSE: find the index value of a given variable name
  PARAMTERS: wm - a WeightMatrix C data structure
             var - the variable name
  RETURN: the index of the variable. if the name of the variable is not
          correct this index will be greater or equal than the number of
          variables, and the function will prompt and error
*/

int
wm_ivar(WeightMatrix* wm, char* var) {
  int i=0;
  int f=0;

  while (i<wm->nvars && !f) {
    f = !strcmp(wm->vars[i],var);
    if (!f)
      i++;
  }

  if (i >= wm->nvars)
    error("variable %s not defined in the weight matrix\n", var);

  return i;
}



/*
  FUNCTION: wm_ival
  PURPOSE: find the index value of a given value name from a variable
  PARAMTERS: wm - a WeightMatrix C data structure
             ivar - index of the variable from which the value index is retrieved
             val - the value name
  RETURN: the index of the value. if the name of the value is not
          correct this index will be greater or equal than the number of
          values of the variable given, and the function will prompt an error
*/

int
wm_ival(WeightMatrix* wm, int ivar, char* val) {
  int i=0;
  int f=0;

  while (i<wm->nvals[ivar] && !f) {
    f = !strcmp(wm->vals[ivar][i],val);
    if (!f)
      i++;
  }

  if (i >= wm->nvals[ivar])
    i = -1;

  return i;
}
