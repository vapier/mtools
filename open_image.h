#include "xdf_io.h"
#define SKIP_PARTITION 1

Stream_t *OpenImage(struct device *out_dev, struct device *dev,
		    const char *name, int mode, char *errmsg,
		    int mode2, int lockMode,
		    mt_size_t *maxSize, int *geomFailureP,
		    int skip
#ifdef USE_XDF
		    , struct xdf_info *xdf_info
#endif
		    );
