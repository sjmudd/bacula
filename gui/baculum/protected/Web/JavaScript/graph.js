var JobClass = jQuery.klass({
	job: null,
	job_size: 0,
	start_stamp: 0,
	end_stamp: 0,
	start_point: [],
	end_point: [],

	initialize: function(job) {
		this.set_job(job);
	},

	set_job: function(job) {
		if (typeof(job) == "object") {
			this.job = job;
			this.set_job_size();
			this.set_start_stamp();
			this.set_end_stamp();
			this.set_start_point();
			this.set_end_point();
		} else {
			alert('Job is not object');
		}
	},

	set_start_point: function() {
		var xaxis = this.start_stamp;
		var yaxis = this.job_size;
		this.start_point = [xaxis, yaxis];
	},

	set_end_point: function() {
		var xaxis = this.end_stamp;
		var yaxis = this.job_size;
		this.end_point = [xaxis, yaxis];
	},

	set_job_size: function(unit) {
		var units = ['B', 'KiB', 'MiB', 'GiB', 'TiB'];
		var pos = units.indexOf(unit);
		var size = 0;

		if (pos != -1) {
			unit = pos;
		} else {
			// default GiB
			unit = 3;
		}

		this.job_size = this.job.jobbytes / Math.pow(1024, unit);
	},

	set_start_stamp: function() {
		/**
		 * NOTE: Start time can be null if job finishes with error before
		 * writing starttime to the Catalog. Noticed for example with staring
		 * migration job in which no SelectionType defined.
		 */
		if (this.job.starttime) {
			this.start_stamp = iso_date_to_timestamp(this.job.starttime);
		}
	},

	set_end_stamp: function() {
		if (this.job.endtime) {
			this.end_stamp =  iso_date_to_timestamp(this.job.endtime);
		}
	}
});

var GraphClass = jQuery.klass({
	jobs: [],
	jobs_all: [],
	series: [],
	graph_obj: null,
	graph_container: null,
	legend_container: null,
	time_range: null,
	date_from: null,
	date_to: null,
	job_filter: null,
	txt: {},
	filter_include: {
		type: 'B'
	},
	filter_exclude: {
		start_stamp: 0,
		end_stamp: 0
	},
	filter_all_mark: '@',
	graph_options:  {
		legend: {
			show: true,
			noColumns: 9,
			labelBoxHeight: 10,
			fontColor: '#000000'
		},
		bars: {
			show: true,
			fill: true,
			horizontal : false,
			shadowSize : 0
		},
		xaxis: {
			mode : 'time',
			timeMode: 'local',
			labelsAngle : 45,
			autoscale: true,
			color: 'black',
			showLabels: true
		},
		yaxis: {
			color: 'black',
			min: 0
		},
		selection: {
			mode : 'x'
		},
		lines: {
			show: true,
			lineWidth: 0,
			fill: true,
			steps: true
		},
		grid: {
			color: '#000000',
			outlineWidth: 0
		},
		HtmlText: false
	},

	initialize: function(jobs, txt, container, legend, time_range, date_from, date_to, client_filter, job_filter) {
		this.set_jobs(jobs);
		this.txt = txt;
		this.set_graph_container(container);
		this.set_legend_container(legend);
		this.set_time_range_filter(time_range);
		this.set_date_from_el(date_from);
		this.set_date_to_el(date_to);
		this.set_date_fields_events(date_from, date_to);
		this.set_jobs_filter(job_filter);
		this.set_clients_filter(client_filter);
		this.set_job_list();
		this.update();
		this.set_events();
	},

	update: function() {
		this.extend_graph_options();
		this.apply_jobs_filter();
		this.prepare_series();
		this.draw_graph();
	},

	set_jobs: function(jobs, filter) {
		if (!filter) {
			var job;
			for (var i = 0; i<jobs.length; i++) {
				if(jobs[i].jobstatus == 'R' || jobs[i].jobstatus == 'C' || jobs[i].endtime == null) {
					continue;
				}
				job = new JobClass(jobs[i]);
				this.jobs.push(job);
			}
			this.jobs_all = this.jobs;
		} else {
			this.jobs = jobs;
		}
	},

	set_job_list: function() {
		var job_list = {};
		for (var i = 0; i < this.jobs_all.length; i++) {
			job_list[this.jobs_all[i].job.name] = 1;
		}
		var job_filter = document.getElementById(this.job_filter);
		for (var job in job_list) {
			var opt = document.createElement('OPTION');
			var label = document.createTextNode(job);
			opt.value = job;
			opt.appendChild(label);
			job_filter.appendChild(opt);
		}
	},

	get_jobs: function() {
		return this.jobs;
	},

	set_graph_container: function(id) {
		this.graph_container = document.getElementById(id);
	},

	get_graph_container: function() {
		return this.graph_container;
	},

	set_legend_container: function(id) {
		this.legend_container = $('#' + id);
	},

	get_legend_container: function() {
		return this.legend_container;
	},

	set_time_range_filter: function(id) {
		var self = this;
		this.time_range = $('#' + id);
		this.time_range.on('change', function() {
			var time_range = self.get_time_range();
			self.set_time_range(time_range);
			self.update();
		});
	},

	get_time_range: function() {
		var time_range = parseInt(this.time_range[0].value, 10) * 1000;
		return time_range;
	},

	set_date_from_el: function(date_from) {
		this.date_from = $('#' + date_from);
	},

	get_date_from_el: function() {
		return this.date_from;
	},

	set_date_to_el: function(date_to) {
		this.date_to = $('#' + date_to);
	},

	get_date_to_el: function() {
		return this.date_to;
	},

	set_time_range: function(timestamp) {
		var to_stamp = Math.round(new Date().getTime());
		this.set_xaxis_max(to_stamp, true);
		var from_stamp = (Math.round(new Date().getTime()) - this.get_time_range());
		this.set_xaxis_min(from_stamp, true);
	},


	set_xaxis_min: function(value, set_range) {
		if (this.graph_options.xaxis.max && value > this.graph_options.xaxis.max) {
			alert('Wrong time range.');
			return;
		}

		if (value == this.graph_options.xaxis.max) {
			value -= 86400000;
		}

		this.graph_options.xaxis.min = value;

		if (set_range) {
			var iso_date = timestamp_to_iso_date(value);
			var from_el = this.get_date_from_el();
			from_el[0].value = iso_date;
		}
	},

	set_xaxis_max: function(value, set_range) {
		if (value < this.graph_options.xaxis.min) {
			alert('Wrong time range.');
			return;
		}

		if (value == this.graph_options.xaxis.min) {
			value += 86400000;
		}

		this.graph_options.xaxis.max = value;

		if (set_range) {
			var iso_date = timestamp_to_iso_date(value);
			var to_el = this.get_date_to_el();
			to_el[0].value = iso_date;
		}
	},

	set_date_fields_events: function(date_from, date_to) {
		var self = this;
		$('#' + date_from).on('change', function() {
			var from_stamp = iso_date_to_timestamp(this.value);
			self.set_xaxis_min(from_stamp);
			self.update();
		});

		$('#' + date_to).on('change', function() {
			var to_stamp = iso_date_to_timestamp(this.value);
			self.set_xaxis_max(to_stamp);
			self.update();
		});

		var date = this.get_time_range();
		this.set_time_range(date);
	},

	set_clients_filter: function(client_filter) {
		var self = this;
		$('#' + client_filter).on('change', function() {
			if (this.value == self.filter_all_mark) {
				delete self.filter_include['clientid'];
			} else {
				self.filter_include['clientid'] = parseInt(this.value, 10);
			}
			self.update();
		});
	},

	set_jobs_filter: function(job_filter) {
		var self = this;
		this.job_filter = job_filter;
		$('#' + job_filter).on('change', function() {
			if (this.value == self.filter_all_mark) {
				delete self.filter_include['name'];
			} else {
				self.filter_include['name'] = this.value;
			}
			self.update();
		});
	},

	apply_jobs_filter: function() {
		var self = this;
		var jobs = this.jobs_all;
		var filtred_jobs = [];
		var to_add;
		for (var i = 0; i < jobs.length; i++) {
			to_add = true;
			jQuery.each(this.filter_include, function(key, value) {
				if (jobs[i].hasOwnProperty(key) && jobs[i][key] != value) {
					to_add = false;
					return;
				}
				if (jobs[i].job.hasOwnProperty(key) && jobs[i].job[key] != value) {
					to_add = false;
					return;
				}
			});
			if (to_add === true) {
				filtred_jobs.push(jobs[i]);
			}
		}

		for (var i = 0; i < filtred_jobs.length; i++) {
			jQuery.each(this.filter_exclude, function(key, value) {
				if (filtred_jobs[i].hasOwnProperty(key) && filtred_jobs[i][key] != value) {
					return;
				}
				if (filtred_jobs[i].job.hasOwnProperty(key) && filtred_jobs[i].job[key] != value) {
					return;
				}
				delete filtred_jobs[i];
			});
		}
		this.set_jobs(filtred_jobs, true);
	},

	prepare_series: function() {
		var self = this;
		this.series = [];
		var series_uniq = {};
		var x_vals = [];
		var y_vals = [];
		var jobs = this.get_jobs();
		for (var i = 0; i < jobs.length; i++) {
			if(jobs[i].start_stamp < this.graph_options.xaxis.min || jobs[i].end_stamp > this.graph_options.xaxis.max) {
				continue;
			}
			if (series_uniq.hasOwnProperty(jobs[i].job.name) == false) {
				series_uniq[jobs[i].job.name] = [];
			}
			series_uniq[jobs[i].job.name].push(jobs[i].start_point, jobs[i].end_point, [null, null]);

		}
		jQuery.each(series_uniq, function(key, value) {
			var serie = [];
			for (var i = 0; i<value.length; i++) {
				serie.push(value[i]);
			}
			self.series.push({data: serie, label: key});
		});
	},

	extend_graph_options: function() {
		this.graph_options.legend.container = this.get_legend_container();
		this.graph_options.title = this.txt.graph_title;
		this.graph_options.xaxis.title = this.txt.xaxis_title;
		this.graph_options.yaxis.title = this.txt.yaxis_title;
	},

	draw_graph: function(opts) {
		var options = Flotr._.extend(Flotr._.clone(this.graph_options), opts || {});

		this.graph_obj = Flotr.draw(
			this.get_graph_container(),
			this.series,
			options
		);
	},

	set_events: function() {
		var self = this;
		Flotr.EventAdapter.observe(this.graph_container, 'flotr:select', function(area) {
			var options = {
				xaxis : {
					min : area.x1,
					max : area.x2,
					mode : 'time',
					timeMode: 'local',
					labelsAngle : 45,
					color: 'black',
					autoscale: true
					},
				yaxis : {
					min : area.y1,
					max : area.y2,
					color: 'black',
					autoscale: true
				}
			};

			self.draw_graph(options);
		});

		Flotr.EventAdapter.observe(this.graph_container, 'flotr:click', function () {
			self.update();
		});
	}
});

var GraphPieClass = jQuery.klass({
	jobs: [],
	container: null,
	series: null,
	pie: null,
	graph_options: {
		colors: ['#63c422', '#d70808', '#FFFF66', 'orange', 'blue'],
		HtmlText: false,
		fontColor: '#000000',
		grid: {
			verticalLines : false,
			horizontalLines : false,
			outlineWidth: 0,
		},
		xaxis: { showLabels : false,},
		yaxis: { showLabels : false },
		pie: {
			show : true,
			explode : 6,
			labelFormatter: PieGraph.pie_label_formatter,
			shadowSize: 4,
			fillOpacity: 1,
			sizeRatio: 0.77
		},
		mouse: {
			track : true,
			trackFormatter: PieGraph.pie_track_formatter,
			relative: false,
			position : 'sw'
		},
		legend: {
			position : 'se',
			backgroundColor : '#D2E8FF',
			margin: 0
		}
	},
	initialize: function(jobs, container_id) {
		this.jobs = jobs;
		this.container = document.getElementById(container_id);
		this.series = this.prepare_series();
		this.draw_grah();
	},
	prepare_series: function() {
		var series = [];
		var label, serie;
		var job_types = Object.keys(this.jobs);
		var jobs_count;
		for (var i = 0; i < job_types.length; i++) {
			label = job_types[i];
			jobs_count = this.jobs[label].length;
			serie = {
				data: [[0, jobs_count]],
				label: label + ' (' + jobs_count.toString() + ')'
			}
			series.push(serie);
		}
		return series;
	},
	draw_grah: function() {
		this.pie = Flotr.draw(this.container, this.series, this.graph_options);
	}
});

var iso_date_to_timestamp = function(iso_date) {
	var date_split = iso_date.split(' ');
	var date = date_split[0].split('-');
	if (date_split[1]) {
		var time = date_split[1].split(':');
		var date_obj = new Date(date[0], (date[1] - 1), date[2], time[0], time[1], time[2]);
	} else {
		var date_obj = new Date(date[0], (date[1] - 1), date[2], 0, 0, 0);
	}
	return date_obj.getTime();
}

var timestamp_to_iso_date = function(timestamp) {
	var iso_date;
	var date = new Date(timestamp);

	var year = date.getFullYear();
	var month = (date.getMonth() + 1).toString();
	month = ('0' + month).substr(-2,2);
	var day = date.getDate().toString();
	day = ('0' + day).substr(-2,2);
	var date_values =  [year, month ,day];

	var iso_date = date_values.join('-');
	return iso_date;
}
