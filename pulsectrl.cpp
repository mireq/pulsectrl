#include <iomanip>
#include <iostream>
#include <locale.h>
#include <pulse/pulseaudio.h>
#include <signal.h>
#include <string>
#include <thread>
#include <unistd.h>


using namespace std;


enum Target {
	TARGET_SINK,
	TARGET_SOURCE
};

enum Action {
	ACTION_TOGGLE_MUTE,
	ACTION_SET_MUTE,
	ACTION_CHANGE_VOLUME,
	ACTION_SET_VOLUME
};

class PulseControl;

struct VolumeAction
{
	Target target;
	Action action;
	float value;
	PulseControl *ctrl;
};

static pa_cvolume volume_control;

class PulseControl
{
public:
	PulseControl();
	~PulseControl();
	bool initialize();
	void run();
	void perform_action(VolumeAction *action);
	void parse_stdin();
	void close();
	void quit_loop(int ret = 0);

private:
	static void signal_quit_callback(pa_mainloop_api *m, pa_signal_event *e, int sig, void *userdata);
	static void context_callback(pa_context *c, void *userdata);
	static void subscribe_callback(pa_context *c, pa_subscription_event_type_t type, uint32_t idx, void *userdata);
	static void server_info_callback(pa_context *c, const pa_server_info *i, void *userdata);
	static void default_sink_info_callback(pa_context *c, const pa_sink_info *i, int eol, void *userdata);
	static void sink_info_callback(pa_context *c, const pa_sink_info *i, int eol, void *userdata);
	static void default_source_info_callback(pa_context *c, const pa_source_info *i, int eol, void *userdata);
	static void source_info_callback(pa_context *c, const pa_source_info *i, int eol, void *userdata);
	static void sink_action_callback(pa_context *c, const pa_sink_info *i, int eol, void *userdata);
	static void source_action_callback(pa_context *c, const pa_source_info *i, int eol, void *userdata);

private:
	pa_mainloop* _mainloop;
	pa_mainloop_api* _mainloop_api;
	pa_signal_event* _sigint;
	pa_context* _context;
	const char* _default_sink_name;
	const char* _default_source_name;
	uint32_t _default_sink_idx;
	uint32_t _default_source_idx;
};


PulseControl::PulseControl():
	_mainloop(nullptr),
	_mainloop_api(nullptr),
	_sigint(nullptr),
	_context(nullptr),
	_default_sink_idx(0),
	_default_source_idx(0)
{
}

bool PulseControl::initialize()
{
	_mainloop = pa_mainloop_new();
	if (!_mainloop) {
		cerr << "pa_mainloop_new() failed" << endl;
		return false;
	}

	_mainloop_api = pa_mainloop_get_api(_mainloop);
	if (!_mainloop_api) {
		cerr << "pa_mainloop_get_api() failed" << endl;
		return false;
	}

	if (pa_signal_init(_mainloop_api) != 0)
	{
		cerr << "pa_signal_init() failed" << endl;
		return false;
	}

	_sigint = pa_signal_new(SIGINT, signal_quit_callback, this);
	if (!_sigint)
	{
		cerr << "pa_signal_new() failed" << endl;
		return false;
	}
	signal(SIGPIPE, SIG_IGN);

	_context = pa_context_new(_mainloop_api, "PulseAudio control");
	if (!_context) {
		cerr << "pa_context_new() failed" << endl;
		return false;
	}

	pa_context_set_state_callback(_context, context_callback, this);

	if (pa_context_connect(_context, NULL, PA_CONTEXT_NOAUTOSPAWN, NULL) < 0) {
		cerr << "pa_context_connect() failed" << endl;
		return false;
	}

	return true;
}

void PulseControl::run() {
	while (1) {
		if (!initialize()) {
			close();
			sleep(2);
			continue;
		}

		int ret = 0;
		if (pa_mainloop_run(_mainloop, &ret) < 0) {
			cerr << "pa_mainloop() failed" << endl;
		}
		close();

		if (ret == 1) {
			return;
		}

		sleep(2);
	}
}

void PulseControl::perform_action(VolumeAction *action)
{
	pa_operation *op = nullptr;

	switch (action->target) {
		case TARGET_SINK:
			op = pa_context_get_sink_info_by_index(_context, _default_sink_idx, sink_action_callback, action);
			break;
		case TARGET_SOURCE:
			op = pa_context_get_source_info_by_index(_context, _default_source_idx, source_action_callback, action);
			break;
	}

	if (op) {
		pa_operation_unref(op);
	}
}

void PulseControl::parse_stdin() {
	for (string line; getline(cin, line);) {
		if (!_context) {
			continue;
		}

		size_t pos = 0;
		string token;
		pos = line.find(' ');
		if (pos == string::npos) {
			continue;
		}

		token = line.substr(0, pos);
		line.erase(0, pos + 1);

		if (token != "source" and token != "sink") {
			continue;
		}

		VolumeAction *action = new VolumeAction();
		action->target = TARGET_SINK;
		action->value = 0.0;
		action->ctrl = this;

		if (token == "source") {
			action->target = TARGET_SOURCE;
		}

		if (line == "mute_toggle") {
			action->action = ACTION_TOGGLE_MUTE;
			perform_action(action);
			continue;
		}
		if (line == "mute_set") {
			action->action = ACTION_SET_MUTE;
			action->value = 1.0;
			perform_action(action);
			continue;
		}
		if (line == "mute_clear") {
			action->action = ACTION_SET_MUTE;
			perform_action(action);
			continue;
		}

		pos = line.find(' ');
		if (pos == string::npos) {
			delete action;
			continue;
		}
		token = line.substr(0, pos);
		line.erase(0, pos + 1);

		try {
			action->value = stof(line);
		}
		catch (invalid_argument& /* ia */) {
			delete action;
			continue;
		}

		if (token == "change") {
			action->action = ACTION_CHANGE_VOLUME;
			perform_action(action);
			continue;
		}
		if (token == "set") {
			action->action = ACTION_SET_VOLUME;
			perform_action(action);
			continue;
		}

		delete action;
	}
}

void PulseControl::close()
{
	if (_context) {
		pa_context_unref(_context);
		_context = nullptr;
	}

	if (_sigint) {
		pa_signal_free(_sigint);
		pa_signal_done();
		_sigint = nullptr;
	}

	if (_mainloop) {
		pa_mainloop_free(_mainloop);
		_mainloop = nullptr;
	}
}

void PulseControl::quit_loop(int ret) {
	if (_mainloop_api) {
		_mainloop_api->quit(_mainloop_api, ret);
	}
}

PulseControl::~PulseControl()
{
	close();
}


// Quit event loop
void PulseControl::signal_quit_callback(pa_mainloop_api * /* mainloop_api */, pa_signal_event * /* event */, int /* sig */, void *userdata)
{
	PulseControl* pulse = static_cast<PulseControl *>(userdata);
	if (pulse) {
		pulse->quit_loop(1);
	}
}

void PulseControl::context_callback(pa_context *c, void *userdata)
{
	if (!c || !userdata) {
		return;
	}

	PulseControl* pulse = static_cast<PulseControl*>(userdata);
	pa_operation *op = nullptr;

	switch (pa_context_get_state(c))
	{
		case PA_CONTEXT_CONNECTING:
		case PA_CONTEXT_AUTHORIZING:
		case PA_CONTEXT_SETTING_NAME:
			break;
		case PA_CONTEXT_READY:
			op = pa_context_get_server_info(c, server_info_callback, userdata);
			pa_context_set_subscribe_callback(c, subscribe_callback, userdata);
			pa_context_subscribe(c, static_cast<pa_subscription_mask>(PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SOURCE | PA_SUBSCRIPTION_MASK_SERVER | PA_SUBSCRIPTION_MASK_CARD), NULL, NULL);
			break;
		default:
			pulse->quit_loop(0);
			break;
	}

	if (op) {
		pa_operation_unref(op);
	}
}


void PulseControl::subscribe_callback(pa_context *c, pa_subscription_event_type_t type, uint32_t idx, void *userdata)
{
	if (!c || !userdata) {
		return;
	}

	unsigned facility = type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;

	pa_operation *op = nullptr;

	switch (facility) {
		case PA_SUBSCRIPTION_EVENT_SINK:
			op = pa_context_get_sink_info_by_index(c, idx, sink_info_callback, userdata);
			break;
		case PA_SUBSCRIPTION_EVENT_SOURCE:
			op = pa_context_get_source_info_by_index(c, idx, source_info_callback, userdata);
			break;
		case PA_SUBSCRIPTION_EVENT_SERVER:
		case PA_SUBSCRIPTION_EVENT_CARD:
			op = pa_context_get_server_info(c, server_info_callback, userdata);
			break;
		default:
			break;
	}

	if (op) {
		pa_operation_unref(op);
	}
}


void PulseControl::server_info_callback(pa_context *c, const pa_server_info *i, void *userdata)
{
	if (!c || !userdata) {
		return;
	}

	pa_operation *op = pa_context_get_sink_info_by_name(c, i->default_sink_name, default_sink_info_callback, userdata);
	if (op) {
		pa_operation_unref(op);
	}

	op = pa_context_get_source_info_by_name(c, i->default_source_name, default_source_info_callback, userdata);
	if (op) {
		pa_operation_unref(op);
	}
}


void PulseControl::default_sink_info_callback(pa_context *c, const pa_sink_info *i, int eol, void *userdata)
{
	if (!c || !i || !userdata) {
		return;
	}

	PulseControl* pulse = static_cast<PulseControl*>(userdata);

	if (i->index != pulse->_default_sink_idx) {
		cout << "default sink\t" << i->name << endl;
		pulse->_default_sink_idx = i->index;
		sink_info_callback(c, i, eol, userdata);
	}
}


void PulseControl::sink_info_callback(pa_context *c, const pa_sink_info *i, int eol, void *userdata)
{
	if (!c || !i || !userdata || eol != 0) {
		return;
	}

	PulseControl* pulse = static_cast<PulseControl*>(userdata);

	char default_flag = ' ';
	char mute_flag = ' ';

	if (i->index == pulse->_default_sink_idx) {
		default_flag = '*';
	}
	if (i->mute) {
		mute_flag = 'M';
	}

	float volume = (float)pa_cvolume_avg(&(i->volume)) / (float)PA_VOLUME_NORM;
	cout << "volume sink\t" << default_flag << mute_flag << '\t' << setw(6) << setprecision(5) << fixed << showpoint << volume << "\t" << i->name << endl;
}


void PulseControl::default_source_info_callback(pa_context *c, const pa_source_info *i, int eol, void *userdata)
{
	if (!c || !i || !userdata) {
		return;
	}

	PulseControl* pulse = static_cast<PulseControl*>(userdata);

	if (i->index != pulse->_default_source_idx) {
		cout << "default source\t" << i->name << endl;
		pulse->_default_source_idx = i->index;
		source_info_callback(c, i, eol, userdata);
	}
}


void PulseControl::source_info_callback(pa_context *c, const pa_source_info *i, int eol, void *userdata)
{
	if (!c || !i || !userdata || eol != 0) {
		return;
	}

	PulseControl* pulse = static_cast<PulseControl*>(userdata);

	char default_flag = ' ';
	char mute_flag = ' ';

	if (i->index == pulse->_default_source_idx) {
		default_flag = '*';
	}
	if (i->mute) {
		mute_flag = 'M';
	}

	float volume = (float)pa_cvolume_avg(&(i->volume)) / (float)PA_VOLUME_NORM;
	cout << "volume source\t" << default_flag << mute_flag << '\t' << setw(6) << setprecision(5) << fixed << showpoint << volume << "\t" << i->name << endl;
}


void PulseControl::sink_action_callback(pa_context *c, const pa_sink_info *i, int eol, void *userdata)
{
	if (!c || !i || !userdata || eol != 0) {
		return;
	}

	volume_control.channels = i->volume.channels;
	for (uint8_t ch = 0; ch < volume_control.channels; ++ch) {
		volume_control.values[ch] = i->volume.values[ch];
	}

	pa_operation *op = nullptr;
	VolumeAction* action = static_cast<VolumeAction *>(userdata);
	float volume;
	switch (action->action) {
		case ACTION_TOGGLE_MUTE:
			op = pa_context_set_sink_mute_by_index(c, i->index, !i->mute, nullptr, nullptr);
			break;
		case ACTION_SET_MUTE:
			op = pa_context_set_sink_mute_by_index(c, i->index, (int)action->value, nullptr, nullptr);
			break;
		case ACTION_CHANGE_VOLUME:
			volume = (float)pa_cvolume_avg(&(i->volume)) / float(PA_VOLUME_NORM);
			volume += action->value;
			if (volume < 0) {
				volume = 0.0;
			}
			if (volume > 1.5) {
				volume = 1.5;
			}
			pa_cvolume_set(&volume_control, volume_control.channels, int(volume * PA_VOLUME_NORM));
			op = pa_context_set_sink_volume_by_index(c, i->index, &volume_control, nullptr, nullptr);
			break;
		case ACTION_SET_VOLUME:
			pa_cvolume_set(&volume_control, volume_control.channels, int(action->value * PA_VOLUME_NORM));
			op = pa_context_set_sink_volume_by_index(c, i->index, &volume_control, nullptr, nullptr);
			break;
	}

	if (op) {
		pa_operation_unref(op);
	}

	delete action;
}

void PulseControl::source_action_callback(pa_context *c, const pa_source_info *i, int eol, void *userdata)
{
	if (!c || !i || !userdata || eol != 0) {
		return;
	}

	volume_control.channels = i->volume.channels;
	for (uint8_t ch = 0; ch < volume_control.channels; ++ch) {
		volume_control.values[ch] = i->volume.values[ch];
	}

	pa_operation *op = nullptr;
	VolumeAction* action = static_cast<VolumeAction *>(userdata);
	float volume;
	switch (action->action) {
		case ACTION_TOGGLE_MUTE:
			op = pa_context_set_source_mute_by_index(c, i->index, !i->mute, nullptr, nullptr);
			break;
		case ACTION_SET_MUTE:
			op = pa_context_set_source_mute_by_index(c, i->index, (int)action->value, nullptr, nullptr);
			break;
		case ACTION_CHANGE_VOLUME:
			volume = (float)pa_cvolume_avg(&(i->volume)) / float(PA_VOLUME_NORM);
			volume += action->value;
			if (volume < 0) {
				volume = 0.0;
			}
			if (volume > 1.5) {
				volume = 1.5;
			}
			pa_cvolume_set(&volume_control, volume_control.channels, int(volume * PA_VOLUME_NORM));
			op = pa_context_set_source_volume_by_index(c, i->index, &volume_control, nullptr, nullptr);
			break;
		case ACTION_SET_VOLUME:
			pa_cvolume_set(&volume_control, volume_control.channels, int(action->value * PA_VOLUME_NORM));
			op = pa_context_set_source_volume_by_index(c, i->index, &volume_control, nullptr, nullptr);
			break;
	}

	if (op) {
		pa_operation_unref(op);
	}

	delete action;
}

int main(int, char *[])
{
	setlocale(LC_NUMERIC, "C");
	auto ctrl = PulseControl();
	std::thread t(&PulseControl::parse_stdin, &ctrl);

	ctrl.run();
	return 0;
}
