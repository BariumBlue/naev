/*
 * See Licensing and Copyright notice in naev.h
 */

/**
 * @file economy.c
 *
 * @brief Handles economy stuff.
 *
 */


#include "economy.h"

#include "naev.h"

#include <stdio.h>
#include "nstring.h"
#include <stdint.h>
#include <limits.h>

#ifdef HAVE_SUITESPARSE_CS_H
#include <suitesparse/cs.h>
#else
#include <cs.h>
#endif

#include "nxml.h"
#include "ndata.h"
#include "log.h"
#include "spfx.h"
#include "pilot.h"
#include "rng.h"
#include "space.h"
#include "ntime.h"
#include "land.h"


#define XML_COMMODITY_ID      "Commodities" /**< XML document identifier */
#define XML_COMMODITY_TAG     "commodity" /**< XML commodity identifier. */


/* commodity stack */
Commodity* commodity_stack = NULL; /**< Contains all the commodities. */
static int commodity_nstack       = 0; /**< Number of commodities in the stack. */


/* systems stack. */
extern StarSystem *systems_stack; /**< Star system stack. */
extern int systems_nstack; /**< Number of star systems. */

/* planet stack  */
extern Planet *planet_stack; /**< Planet stack. */
extern int planet_nstack; /**< Num of planets */
extern int commodity_mod;  /**< Smallest unit the player can buy, for player prices */

float **xml_prices = NULL; /* an array of: array of all systems's original prices, NULL if none */

static int econ_initialized   = 0; /**< Is economy system initialized? */
static int *econ_comm         = NULL; /**< Commodities to calculate. */
int econ_nprices              = 0; /**< Number of prices to calculate. */

char show_prices; /* whether or not to show prices on the map, default 0 */

/*
 * Prototypes.
 */
int econ_refreshcommprice(void);   /* Refresh the prices of a commodity */
/* Commodity. */
static void commodity_freeOne( Commodity* com );
static int commodity_parse( Commodity *temp, xmlNodePtr parent );



/**
 * @brief Converts credits to a usable string for displaying.
 *
 *    @param[out] str Output is stored here, must have at least a length of 32
 *                     char.
 *    @param credits Credits to display.
 *    @param decimals Decimals to use.
 */
void credits2str( char *str, credits_t credits, int decimals )
{
   if (decimals < 0)
      nsnprintf( str, ECON_CRED_STRLEN, "%"CREDITS_PRI, credits );
   else if (credits >= 1000000000000000LL)
      nsnprintf( str, ECON_CRED_STRLEN, "%.*fQ", decimals, (double)credits / 1000000000000000. );
   else if (credits >= 1000000000000LL)
      nsnprintf( str, ECON_CRED_STRLEN, "%.*fT", decimals, (double)credits / 1000000000000. );
   else if (credits >= 1000000000L)
      nsnprintf( str, ECON_CRED_STRLEN, "%.*fB", decimals, (double)credits / 1000000000. );
   else if (credits >= 1000000)
      nsnprintf( str, ECON_CRED_STRLEN, "%.*fM", decimals, (double)credits / 1000000. );
   else if (credits >= 1000)
      nsnprintf( str, ECON_CRED_STRLEN, "%.*fK", decimals, (double)credits / 1000. );
   else
      nsnprintf (str, ECON_CRED_STRLEN, "%"CREDITS_PRI, credits );
}


/**
 * @brief Gets a commodity by name.
 *
 *    @param name Name to match.
 *    @return Commodity matching name.
 */
Commodity* commodity_get( const char* name )
{
   int i;
   if (name==NULL){
      WARN("Commodity name is NULL!\n");
      return NULL;
   }
   for (i=0; i<commodity_nstack; i++)
      if (strcmp(commodity_stack[i].name,name)==0)
         return &commodity_stack[i];

   WARN("Commodity '%s' not found in stack", name);
   return NULL;
}

/**
 * @brief Gets a commodity by name without warning.
 *
 *    @param name Name to match.
 *    @return Commodity matching name.
 */
Commodity* commodity_getW( const char* name )
{
   int i;
   if (name==NULL) return NULL;
   for (i=0; i<commodity_nstack; i++)
      if (strcmp(commodity_stack[i].name,name)==0)
         return &commodity_stack[i];
   return NULL;
}


/**
 * @brief Frees a commodity.
 *
 *    @param com Commodity to free.
 */
static void commodity_freeOne( Commodity* com )
{
   if (com->name) 
      free(com->name);
   if (com->description)
      free(com->description);

   /* Clear the memory. */
   memset(com, 0, sizeof(Commodity));
}


/**
 * @brief Function meant for use with C89, C99 algorithm qsort().
 *
 *    @param commodity1 First argument to compare.
 *    @param commodity2 Second argument to compare.
 *    @return -1 if first argument is inferior, +1 if it's superior, 0 if ties.
 */
int commodity_compareTech( const void *commodity1, const void *commodity2 )
{
   const Commodity *c1, *c2;

   /* Get commodities. */
   c1 = * (const Commodity**) commodity1;
   c2 = * (const Commodity**) commodity2;

   /* Compare price. */
   if (c1->price < c2->price)
      return +1;
   else if (c1->price > c2->price)
      return -1;

   /* It turns out they're the same. */
   return strcmp( c1->name, c2->name );
}


/**
 * @brief Loads a commodity.
 *
 *    @param temp Commodity to load data into.
 *    @param parent XML node to load from.
 *    @return Commodity loaded from parent.
 */
static int commodity_parse( Commodity *temp, xmlNodePtr parent )
{
   xmlNodePtr node;

   /* Clear memory. */
   memset( temp, 0, sizeof(Commodity) );

   /* Get name. */
   xmlr_attr( parent, "name", temp->name );
   if (temp->name == NULL)
      WARN("Commodity from "COMMODITY_DATA_PATH" has invalid or no name");

   /* Parse body. */
   node = parent->xmlChildrenNode;
   do {
      xml_onlyNodes(node);
      xmlr_strd(node, "description", temp->description);
      xmlr_int(node, "price", temp->price);
      WARN("Commodity '%s' has unknown node '%s'.", temp->name, node->name);
   } while (xml_nextNode(node));

   return 0;
}


/**
 * @brief Throws cargo out in space graphically.
 *
 *    @param pilot ID of the pilot throwing the stuff out
 *    @param com Commodity to throw out.
 *    @param quantity Quantity thrown out.
 */
void commodity_Jettison( int pilot, Commodity* com, int quantity )
{
   (void)com;
   int i;
   Pilot* p;
   int n, effect;
   double px,py, bvx, bvy, r,a, vx,vy;

   p   = pilot_get( pilot );

   n   = MAX( 1, RNG(quantity/10, quantity/5) );
   px  = p->solid->pos.x;
   py  = p->solid->pos.y;
   bvx = p->solid->vel.x;
   bvy = p->solid->vel.y;
   for (i=0; i<n; i++) {
      effect = spfx_get("cargo");

      /* Radial distribution gives much nicer results */
      r  = RNGF()*25 - 12.5;
      a  = 2. * M_PI * RNGF();
      vx = bvx + r*cos(a);
      vy = bvy + r*sin(a);

      /* Add the cargo effect */
      spfx_add( effect, px, py, vx, vy, SPFX_LAYER_BACK );
   }
}


/**
 * @brief Loads all the commodity data.
 *
 *    @return 0 on success.
 */
int commodity_load (void)
{
   uint32_t bufsize;
   char *buf;
   xmlNodePtr node;
   xmlDocPtr doc;

   /* Load the file. */
   buf = ndata_read( COMMODITY_DATA_PATH, &bufsize);
   if (buf == NULL)
      return -1;

   /* Handle the XML. */
   doc = xmlParseMemory( buf, bufsize );
   if (doc == NULL) {
      WARN("'%s' is not valid XML.", COMMODITY_DATA_PATH);
      return -1;
   }

   node = doc->xmlChildrenNode; /* Commodities node */
   if (strcmp((char*)node->name,XML_COMMODITY_ID)) {
      ERR("Malformed "COMMODITY_DATA_PATH" file: missing root element '"XML_COMMODITY_ID"'");
      return -1;
   }

   node = node->xmlChildrenNode; /* first faction node */
   if (node == NULL) {
      ERR("Malformed "COMMODITY_DATA_PATH" file: does not contain elements");
      return -1;
   }

   do {
      xml_onlyNodes(node);
      if (xml_isNode(node, XML_COMMODITY_TAG)) {

         /* Make room for commodity. */
         commodity_stack = realloc(commodity_stack,
               sizeof(Commodity)*(++commodity_nstack));

         /* Load commodity. */
         commodity_parse(&commodity_stack[commodity_nstack-1], node);

         /* See if should get added to commodity list. */
         if (commodity_stack[commodity_nstack-1].price > 0.) {
            commodity_stack[econ_nprices].index=econ_nprices;
            econ_nprices++;
            econ_comm = realloc(econ_comm, econ_nprices * sizeof(int));
            econ_comm[econ_nprices-1] = commodity_nstack-1;
         }
      }
      else
         WARN("'"COMMODITY_DATA_PATH"' has unknown node '%s'.", node->name);
   } while (xml_nextNode(node));

   xmlFreeDoc(doc);
   free(buf);

   DEBUG("Loaded %d Commodit%s", commodity_nstack, (commodity_nstack==1) ? "y" : "ies" );

   return 0;
}


/**
 * @brief Frees all the loaded commodities.
 */
void commodity_free (void)
{
   int i;
   for (i=0; i<commodity_nstack; i++)
      commodity_freeOne( &commodity_stack[i] );
   free( commodity_stack );
   commodity_stack = NULL;
   commodity_nstack = 0;

   /* More clean up. */
   free( econ_comm );
}

/**
 * @brief sets whether to show prices on the map
 */
void set_showPrice(char boolean)
{
   show_prices = boolean;
}


/**
 * @brief update the prices of a commodity, for any system that does 
 *    not have a set price
 *    @return 0 on success.
 */
int econ_refreshcommprice(void)
{
   StarSystem *sys, *nsys;
   int s, c, j, jmp;
   float n_neighbors;
   float total;

   for (s=0; s<systems_nstack; s++){
      sys = systems_stack+s;

      for (c=0; c<econ_nprices; c++){

         if (sys->is_priceset[c])
            continue;
         total=0.0;
         n_neighbors=0.0;

            /* get number of trading neighbors and the sum of their prices */
         for (jmp=0; jmp<sys->njumps; jmp++){
            if (jp_isFlag( sys->jumps+jmp, JP_EXITONLY) || jp_isFlag(sys->jumps+jmp, JP_HIDDEN))
               continue;
            nsys = sys->jumps[jmp].target; /* neighboring system */
            for (j=0; j<nsys->njumps; j++){ /* check that the jump back is valid */
               if (sys == nsys->jumps[j].target){
                  if (!jp_isFlag( nsys->jumps+j, JP_EXITONLY ) && !jp_isFlag(sys->jumps+jmp, JP_HIDDEN)){
                     total+=nsys->prices[c];
                     n_neighbors+=1.0;
                  }
                  break;
               }
            }
         }
            /* put in the new price */
         // printf("price:%f\n\ttotal:%f\n\tneighbors:%f\n",total/n_neighbors, total, n_neighbors);
         sys->prices[c] = total/n_neighbors;
      }
   }

   return 0;
}


/**
 * @brief For every commodity that has had prices changed, update the prices
 *    of any system with no set price
 */
void econ_updateprices(void)
{
   //int i;
   //for (i=0; i<60; i++)
   econ_refreshcommprice();
}

/**
 * @brief revert all economic values to their original, XML designated values
 */
void econ_revert(void)
{
   int i, c;
   StarSystem *sys;

      /* revert to the old values */
   for (i=0; i<systems_nstack; i++){
      sys = systems_stack+i;
      if (xml_prices[i]==NULL){
         for (c=0; c<econ_nprices; c++)
            sys->is_priceset[c]=0;
         continue;
      }
      for (c=0; c<econ_nprices; c++){
         if (xml_prices[i][c]>0.0){
            sys->prices[c]=xml_prices[i][c];
            sys->is_priceset[c]=1;
         }
         else
            sys->is_priceset[c]=0;
      }
   }
   for (c=0; c<econ_nprices; c++)
      commodity_stack[c].changed=1;
   set_showPrice(0);

   return;
}

/**
 * @brief initializes all economic variables. Called immediately after starting galaxy
 */
void econ_init(void)
{
   int s, c;
   StarSystem *sys;

   if (econ_initialized){ WARN("economy already initialized!\n"); return;}

      /* save original values */
   if (xml_prices==NULL)
      xml_prices = (float **) calloc(sizeof(float *), systems_nstack);
   for (s=0; s<systems_nstack; s++){
      sys = systems_stack+s;
      for (c=0; c<econ_nprices; c++){
         if (sys->is_priceset[c]==0)
            continue;
         if (xml_prices[s]==NULL)
            xml_prices[s] = (float *) calloc(sizeof(float), econ_nprices);
         xml_prices[s][c] = sys->prices[c];
      }
   }

   econ_initialized = 1;
}

/**
 * @brief frees all economy related variables. Only use when exiting naev
 */
void econ_destroy(void)
{
   int s;
   StarSystem *sys;
   if (econ_initialized!=1){ WARN("economy not inited!\n"); return; }
   for (s=0; s<systems_nstack; s++){
      sys=systems_stack+s;
      free(sys->prices); 
      free(xml_prices[s]);
      sys->prices=NULL;
   }
   free(xml_prices);
   econ_initialized=0;
}





