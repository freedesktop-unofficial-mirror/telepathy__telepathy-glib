#include "config.h"

#include <telepathy-logger/observer-internal.h>

#include "tests/lib/util.h"

typedef struct {
    int dummy;
} Fixture;

static void
setup (Fixture *fixture,
    gconstpointer data)
{
}

static void
test (Fixture *fixture,
    gconstpointer data)
{
  TplObserver *obs, *obs2;

  obs = _tpl_observer_dup (NULL);

  /* TplObserver is a singleton, be sure both references point to the same
   * memory address  */
  obs2 = _tpl_observer_dup (NULL);
  g_assert (obs == obs2);

  /* unref the second singleton pointer and check that the it is still
   * valid: checking correct object ref-counting after each _dup () call */
  g_object_unref (obs2);
  g_assert (TPL_IS_OBSERVER (obs));

  /* it points to the same mem area, it should be still valid */
  g_assert (TPL_IS_OBSERVER (obs2));


  /* FIXME: This test does not actually test anything useful */

  /* proper disposal for the singleton when no references are present */
  g_object_unref (obs);
}

static void
teardown (Fixture *fixture,
    gconstpointer data)
{
}

int
main (int argc,
    char **argv)
{
  tp_tests_init (&argc, &argv);

  g_test_add ("/log-store-sqlite", Fixture, NULL, setup, test, teardown);

  return tp_tests_run_with_bus ();
}
