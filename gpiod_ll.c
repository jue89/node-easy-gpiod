#include <node_api.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/gpio.h>

#define MAX_LINES 32
#define MAX_EVENTS 128

struct event_observer {
	int fd;
	size_t ptr;
	struct gpio_v2_line_event evts[MAX_EVENTS];
	napi_threadsafe_function cb;
	pthread_t thr;
};

#define EXPECT_ARGC(CNT) napi_value args[CNT]; { \
	size_t argc = CNT; \
	napi_status status = napi_get_cb_info(env, cbinfo, &argc, args, NULL, NULL); \
	if (status != napi_ok) return NULL; \
	if (argc != CNT) return NULL; }

#define EXPECT_CTX(TYPE, NAME) TYPE *NAME; { \
	napi_status status = napi_get_cb_info(env, cbinfo, NULL, NULL, NULL, (void **) &NAME); \
	if (status != napi_ok) return NULL; }

#define GET_ARG_INT32(IDX, NAME) int32_t NAME; { \
	napi_status status = napi_get_value_int32(env, args[IDX], &NAME); \
	if (status != napi_ok) return NULL; }

#define GET_ARG_UINT32(IDX, NAME) uint32_t NAME; { \
	napi_status status = napi_get_value_uint32(env, args[IDX], &NAME); \
	if (status != napi_ok) return NULL; }

#define GET_ARG_BOOL(IDX, NAME) bool NAME; { \
	napi_status status = napi_get_value_bool(env, args[IDX], &NAME); \
	if (status != napi_ok) return NULL; }

#define GET_ARG_STRING(IDX, NAME, LEN) char NAME[LEN]; { \
	napi_status status = napi_get_value_string_utf8(env, args[IDX], NAME, LEN, NULL); \
	if (status != napi_ok) return NULL; }

#define GET_ARG_ARRAY(IDX, NAME) napi_value NAME = args[IDX]; { \
	bool is_array; \
	napi_status status = napi_is_array(env, NAME, &is_array); \
	if (status != napi_ok) return NULL; \
	if (!is_array) return NULL; }

#define GET_ARG_FN(IDX, NAME) napi_value NAME = args[IDX]

#define GET_ARG_STRUCT(IDX, TYPE, NAME) TYPE *NAME; { \
	size_t len; \
	napi_status status = napi_get_buffer_info(env, args[IDX], (void**) &NAME, &len); \
	if (status != napi_ok) return NULL; \
	if (len != sizeof(TYPE)) return NULL; }

#define FOR_EACH(ARR, ITEM, IDX) \
	uint32_t ARR##_len; \
	napi_get_array_length(env, ARR, &ARR##_len); \
	napi_value ITEM; \
	for (size_t IDX = 0; IDX < ARR##_len && napi_get_element(env, ARR, IDX, &ITEM) == napi_ok; IDX++)

#define NEW_OBJECT(OBJ) napi_value OBJ; { \
	napi_status status = napi_create_object(env, &OBJ); \
	if (status != napi_ok) return NULL; }

#define SET_STRING(OBJ, KEY, VALUE) { \
	napi_status status; \
	napi_value str; \
	status = napi_create_string_utf8(env, VALUE, NAPI_AUTO_LENGTH, &str); \
	if (status != napi_ok) return NULL; \
	status = napi_set_named_property(env, OBJ, KEY, str); \
	if (status != napi_ok) return NULL; }

#define SET_UINT32(OBJ, KEY, VALUE) { \
	napi_status status; \
	napi_value val; \
	status = napi_create_uint32(env, VALUE, &val); \
	if (status != napi_ok) return NULL; \
	status = napi_set_named_property(env, OBJ, KEY, val); \
	if (status != napi_ok) return NULL; }

#define SET_INT32(OBJ, KEY, VALUE) { \
	napi_status status; \
	napi_value val; \
	status = napi_create_int32(env, VALUE, &val); \
	if (status != napi_ok) return NULL; \
	status = napi_set_named_property(env, OBJ, KEY, val); \
	if (status != napi_ok) return NULL; }

#define SET_UINT64(OBJ, KEY, VALUE) { \
	napi_status status; \
	napi_value val; \
	status = napi_create_bigint_uint64(env, VALUE, &val); \
	if (status != napi_ok) return NULL; \
	status = napi_set_named_property(env, OBJ, KEY, val); \
	if (status != napi_ok) return NULL; }

#define SET_BOOL(OBJ, KEY, VALUE) { \
	napi_status status; \
	napi_value val; \
	status = napi_get_boolean(env, VALUE, &val); \
	if (status != napi_ok) return NULL; \
	status = napi_set_named_property(env, OBJ, KEY, val); \
	if (status != napi_ok) return NULL; }

#define SET_OBJ(OBJ, KEY, VALUE) { \
	napi_status status = napi_set_named_property(env, OBJ, KEY, VALUE); \
	if (status != napi_ok) return NULL; }

#define SET_FN(OBJ, KEY, FN, CTX) { \
	napi_status status; \
	napi_value fn; \
	status = napi_create_function(env, KEY, NAPI_AUTO_LENGTH, FN, CTX, &fn); \
	if (status != napi_ok) return NULL; \
	status = napi_set_named_property(env, OBJ, KEY, fn); \
	if (status != napi_ok) return NULL; }

#define GET_UINT32(OBJ, KEY, VAR) uint32_t VAR; { \
	napi_status status; \
	napi_value value; \
	status = napi_get_named_property(env, OBJ, KEY, &value); \
	if (status != napi_ok) return NULL; \
	status = napi_get_value_uint32(env, value, &VAR); \
	if (status != napi_ok) return NULL; }

#define GET_BOOL(OBJ, KEY, VAR) bool VAR; { \
	napi_status status; \
	napi_value value; \
	status = napi_get_named_property(env, OBJ, KEY, &value); \
	if (status != napi_ok) return NULL; \
	status = napi_get_value_bool(env, value, &VAR); \
	if (status != napi_ok) return NULL; }

static napi_value GetChipInfo (napi_env env, napi_callback_info cbinfo) {
	EXPECT_ARGC(1);
	GET_ARG_INT32(0, fd);

	struct gpiochip_info info_raw;
	int rc = ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &info_raw);
	if (rc != 0) return NULL;

	NEW_OBJECT(info);
	SET_STRING(info, "name", info_raw.name);
	SET_STRING(info, "label", info_raw.label);
	SET_UINT32(info, "line_cnt", info_raw.lines);

	return info;
}

static napi_value GetLineInfo (napi_env env, napi_callback_info cbinfo) {
	EXPECT_ARGC(2);
	GET_ARG_INT32(0, fd);
	GET_ARG_UINT32(1, offset);

	struct gpio_v2_line_info info_raw = { 0 };
	info_raw.offset = offset;
	int rc = ioctl(fd, GPIO_V2_GET_LINEINFO_IOCTL, &info_raw);
	if (rc != 0) return NULL;

	NEW_OBJECT(flags);
	SET_BOOL(flags, "used", (info_raw.flags & GPIO_V2_LINE_FLAG_USED));
	SET_BOOL(flags, "active_low", (info_raw.flags & GPIO_V2_LINE_FLAG_ACTIVE_LOW)); 
	SET_BOOL(flags, "input", (info_raw.flags & GPIO_V2_LINE_FLAG_INPUT)); 
	SET_BOOL(flags, "output", (info_raw.flags & GPIO_V2_LINE_FLAG_OUTPUT)); 
	SET_BOOL(flags, "pull_up", (info_raw.flags & GPIO_V2_LINE_FLAG_BIAS_PULL_UP)); 
	SET_BOOL(flags, "pull_down", (info_raw.flags & GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN)); 
	NEW_OBJECT(info);
	SET_STRING(info, "name", info_raw.name);
	SET_STRING(info, "consumer", info_raw.consumer);
	SET_OBJ(info, "flags", flags);

	return info;
}

// Will be invoked by the user when the request shall be dropped
static napi_value reqRelease (napi_env env, napi_callback_info cbinfo) {
	EXPECT_CTX(struct event_observer, ctx);
	
	// Will invoke the clean-up method which frees memory and closes fds
	pthread_cancel(ctx->thr);

	return NULL;
}

// Converts interval event struct to JS object
static napi_value evt2obj (napi_env env, struct gpio_v2_line_event *evt) {
	NEW_OBJECT(info);
	SET_UINT64(info, "timestamp_ns", evt->timestamp_ns);
	SET_BOOL(info, "rising_edge", (evt->id == GPIO_V2_LINE_EVENT_RISING_EDGE));
	SET_UINT32(info, "offset", evt->offset);
	SET_UINT32(info, "seqno", evt->seqno);
	SET_UINT32(info, "line_seqno", evt->line_seqno);

	return info;
}

// Will be invoked via lineObserver but run on the V8 event loop
static void lineObserverInvokeCb (napi_env env, napi_value cb, void* context, void* data) {
	if (cb == NULL) return;

	napi_value info = evt2obj(env, data);
	if (info == NULL) return;

	napi_value global;
	napi_status status = napi_get_global(env, &global);
	if (status != napi_ok) return;

	napi_value ret_val;
	napi_call_function(env, global, cb, 1, &info, &ret_val);
}

// Will be invoked after lineObserver finished
static void lineObserverCleanup (void *arg) {
	struct event_observer *ctx = arg;

	napi_release_threadsafe_function(ctx->cb, napi_tsfn_abort);
	close(ctx->fd);
	free(ctx);
}

// Thread reading from event fd
static void * lineObserver (void *arg) {
	struct event_observer *ctx = arg;

	napi_acquire_threadsafe_function(ctx->cb);
	pthread_cleanup_push(lineObserverCleanup, arg);
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);	

	while (true) {
		struct gpio_v2_line_event *evt = &ctx->evts[ctx->ptr++ % MAX_EVENTS];

		ssize_t len = read(ctx->fd, evt, sizeof(struct gpio_v2_line_event));
		if (len != sizeof(struct gpio_v2_line_event)) break;

		// Will block if all slots in the ring-buffer are in-flight
		napi_status status = napi_call_threadsafe_function(ctx->cb, (void *) evt, napi_tsfn_blocking);
		if (status != napi_ok) break;
	}

	// invoke clean-up directly
	pthread_cleanup_pop(1);

	return NULL;
}

static napi_value RequestLines (napi_env env, napi_callback_info cbinfo) {
	EXPECT_ARGC(5);
	GET_ARG_INT32(0, fd);
	GET_ARG_STRING(1, consumer, GPIO_MAX_NAME_SIZE);
	GET_ARG_ARRAY(2, offsets);
	GET_ARG_ARRAY(3, attrs);
	GET_ARG_FN(4, on_event);

	struct gpio_v2_line_request req = { 0 };

	memcpy(&req.consumer, consumer, sizeof(consumer));

	FOR_EACH(offsets, offset, offset_idx) {
		if (offset_idx >= MAX_LINES) return NULL;

		napi_status status = napi_get_value_uint32(env, offset, &req.offsets[offset_idx]);
		if (status != napi_ok) return NULL;

		req.num_lines += 1;
	}

	FOR_EACH(attrs, attr, attr_idx) {
		if (attr_idx >= GPIO_V2_LINE_NUM_ATTRS_MAX) return NULL;

		GET_UINT32(attr, "mask", mask);
		req.config.attrs[attr_idx].mask = mask;

		GET_UINT32(attr, "type", type);
		if (type == 1) {
			uint64_t flags = 0;

			GET_BOOL(attr, "active_low", active_low);
			if (active_low) flags |= GPIO_V2_LINE_FLAG_ACTIVE_LOW;

			GET_BOOL(attr, "output", output);
			if (output) {
				flags |= GPIO_V2_LINE_FLAG_OUTPUT;
				
				GET_UINT32(attr, "drive", drive);
				if (drive == 1) {
					flags |= GPIO_V2_LINE_FLAG_OPEN_DRAIN;
				} else if (drive == 2) {
					flags |= GPIO_V2_LINE_FLAG_OPEN_SOURCE;
				}
			} else {
				flags |= GPIO_V2_LINE_FLAG_INPUT;

				GET_UINT32(attr, "bias", bias);
				if (bias == 1) {
					flags |= GPIO_V2_LINE_FLAG_BIAS_PULL_UP;
				} else if (bias == 2) {
					flags |= GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN;
				} else {
					flags |= GPIO_V2_LINE_FLAG_BIAS_DISABLED;
				}

				GET_BOOL(attr, "rising_edge", rising_edge);
				if (rising_edge) flags |= GPIO_V2_LINE_FLAG_EDGE_RISING;
				GET_BOOL(attr, "falling_edge", falling_edge);
				if (falling_edge) flags |= GPIO_V2_LINE_FLAG_EDGE_FALLING;
			}
			req.config.attrs[attr_idx].attr.id = GPIO_V2_LINE_ATTR_ID_FLAGS;
			req.config.attrs[attr_idx].attr.flags = flags;
		} else if (type == 2) {
			GET_UINT32(attr, "values", values);
			req.config.attrs[attr_idx].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
			req.config.attrs[attr_idx].attr.values = values;
		} else if (type == 3) {
			GET_UINT32(attr, "debounce", debounce_period_us);
			req.config.attrs[attr_idx].attr.id = GPIO_V2_LINE_ATTR_ID_DEBOUNCE;
			req.config.attrs[attr_idx].attr.debounce_period_us = debounce_period_us;
		} else {
			return NULL;
		}

		req.config.num_attrs += 1;
	}

	int rc = ioctl(fd, GPIO_V2_GET_LINE_IOCTL, &req);
	if (rc != 0) return NULL;

	struct event_observer *ctx = malloc(sizeof(struct event_observer));
	ctx->fd = req.fd;
	ctx->ptr = 0;
	napi_value res_name;
	napi_status status = napi_create_string_utf8(env, "observer", NAPI_AUTO_LENGTH, &res_name);
	if (status != napi_ok) return NULL;
	status = napi_create_threadsafe_function(
		env,
		on_event,
		NULL,
		res_name,
		MAX_EVENTS - 1, // Ensures that the ring-buffer always has space for further events
		1, // Main loop
		NULL,
		NULL,
		(void *) ctx,
		lineObserverInvokeCb,
		&ctx->cb
	);
	if (status != napi_ok) return NULL;
	rc = pthread_create(&ctx->thr, NULL, lineObserver, (void *) ctx);
	if (rc != 0) return NULL;

	NEW_OBJECT(info);
	SET_INT32(info, "fd", req.fd);
	SET_FN(info, "release", reqRelease, (void*) ctx);

	return info;
}

static napi_value GetValues (napi_env env, napi_callback_info cbinfo) {
	EXPECT_ARGC(2);
	GET_ARG_INT32(0, fd);
	GET_ARG_UINT32(1, mask);

	struct gpio_v2_line_values req = { 0 };
	req.mask = mask;

	int rc = ioctl(fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &req);
	if (rc != 0) return NULL;

	napi_value values;
	napi_status status = napi_create_uint32(env, (uint32_t) req.bits, &values);
	if (status != napi_ok) return NULL;

	return values;
}

static napi_value SetValues (napi_env env, napi_callback_info cbinfo) {
	EXPECT_ARGC(3);
	GET_ARG_INT32(0, fd);
	GET_ARG_UINT32(1, mask);
	GET_ARG_UINT32(2, bits);

	struct gpio_v2_line_values req = { 0 };
	req.mask = mask;
	req.bits = bits;

	int rc = ioctl(fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &req);
	if (rc != 0) return NULL;

	napi_value ret;
	napi_get_null(env, &ret);
	return ret;
}

#define EXPORT_FN(NAME) { \
	napi_status status; \
	napi_value fn; \
	status = napi_create_function(env, NULL, 0, NAME, NULL, &fn); \
	if (status != napi_ok) return NULL; \
	status = napi_set_named_property(env, exports, #NAME, fn); \
	if (status != napi_ok) return NULL; }

static napi_value Init(napi_env env, napi_value exports) {
	EXPORT_FN(GetChipInfo);
	EXPORT_FN(GetLineInfo);
	EXPORT_FN(RequestLines);
	EXPORT_FN(GetValues);
	EXPORT_FN(SetValues);

	return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init) 
