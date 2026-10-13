#include <stdlib.h>
#include "json.h"
#include "test.h"

void test_json(void)
{
    char *buf = NULL;
    size_t len = 0;
    FILE *f = open_memstream(&buf, &len);           // a FILE that writes into memory
    Json j = json_new(f);
    json_obj(&j);
    json_key(&j, "name");  json_str(&j, "say \"hi\"\n\\ ok");
    json_key(&j, "none");  json_str(&j, NULL);
    json_key(&j, "empty"); json_arr(&j); json_end(&j);
    json_key(&j, "list");  json_arr(&j); json_int(&j, 1); json_bool(&j, false); json_end(&j);
    json_end(&j);
    json_done(&j);
    fclose(f);

    CHECK_STR(buf,
              "{\n"
              "  \"name\": \"say \\\"hi\\\"\\n\\\\ ok\",\n"
              "  \"none\": null,\n"
              "  \"empty\": [],\n"
              "  \"list\": [\n"
              "    1,\n"
              "    false\n"
              "  ]\n"
              "}\n");
    free(buf);
}
