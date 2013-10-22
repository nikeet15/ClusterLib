#!/usr/bin/python

from VectorMath import *
from optparse import OptionParser
from matplotlib import pyplot,lines
import numpy, os, pickle, sys, time, itertools
from OmnetReader import DataContainer


##################
# DATA COMPILING #
##################


# Parse the options passed to the data compiler.
def parseDataCompileOptions( argv ):
	optParser = OptionParser()
	optParser.add_option("-d",  "--directory",  dest="directory", help="Define the results directory.")
	optParser.add_option("-t",     "--useTar",     dest="useTar", help="Define whether the individual runs are compressed into tar files.", action="store_true")
	optParser.add_option("-p",    "--process",    dest="process", help="Specify whether to process 'location', 'grid', or 'highway'.", type='string', default='location' )
	optParser.add_option("-o", "--outputFile", dest="outputFile", help="Specify the output file")
	(options, args) = optParser.parse_args(argv)

	if not options.directory:
		print "Please specify results directory."
		optParser.print_help()
		sys.exit()

	if not options.outputFile:
		print "Please specify output file for compiled data."
		optParser.print_help()
		sys.exit()

	return options


# Enumerate all the configurations
def enumerateConfigs( directoryName, useTar ):
	if useTar:
		configNames = list(set([f.split("-")[0] for f in os.listdir(directoryName) if "tar" in f]))
	else:
		configNames = list(set([f.split("-")[0] for f in os.listdir(directoryName) if "sca" in f]))
	dataContainers = {}
	for config in configNames:
		dataContainers[config] = DataContainer( config, directoryName, useTar )
	return dataContainers

# Collect the results from the given data container.
def collectResults( dataContainer ):
	results = { "overhead" : [], "helloOverhead" : [], "clusterLifetime" : [] , "clusterSize" : [], "headChange" : [] }

	# Get the list of scalars
	scalarList = dataContainer.getScalarList()
	for scalarDef in scalarList:
		moduleName = scalarDef[0]
		scalarName = scalarDef[1]

		if "net" not in moduleName:
			continue

		resName = scalarName.split(":")[0]
		if resName not in results:
			continue

		scalar = dataContainer.getScalar( moduleName, scalarName )
		if numpy.isnan( scalar.value ):
			continue

		results[resName].append( scalar.value )

	# Get the list of statistics
	statisticsList = dataContainer.getStatisticsList()
	for statisic in statisticsList:
		moduleName = statisic[0]
		statName = statisic[1]

		if "net" not in moduleName:
			continue

		resName = statName.split(":")[0]
		if resName not in results:
			continue

		stat = dataContainer.getStatistic( moduleName, statName )
		if 'count' not in stat.fields or 'sum' not in stat.fields or 'mean' not in stat.fields:
			continue

		if stat.fields['count'] == 0:
			continue

		if resName == 'overhead' or resName == 'helloOverhead':
			results[resName].append( stat.fields['sum'] )
		else:
			results[resName].append( stat.fields['mean'] )

	colatedResults = {}
	for key in results.iterkeys():
		colatedResults[key + " Mean"] = numpy.mean( results[key] )
		colatedResults[key +  " Max"] = max( results[key] )
		colatedResults[key +  " Min"] = min( results[key] )

	return colatedResults

# Compile the data.
def dataCompile( argv ):
	options = parseDataCompileOptions(argv)
	# Enumerate all configurations.
	dataContainers = enumerateConfigs( options.directory, options.useTar )
	startTime = time.clock()
	resultSet = {}

	# Iterate through all the configurations
	for config in dataContainers:
		# Get the list of runs
		runList = sorted(dataContainers[config].getRunList())
		print "Found " + str(len(runList)) + " runs for " + config + "."

		if ( ( "grid" in config ) != ( options.process == 'grid' ) ) or ( ( "highway" in config ) != ( options.process == 'highway' ) ):
			if options.process == 'grid':
				print "We've been asked to only process grid sims, and " + config + " is not one. Skipping."
			elif options.process == 'highway':
				print "We've been asked to only process highway sims, and " + config + " is not one. Skipping."
			elif options.process == 'location':
				print "We've been asked to only process location sims, and " + config + " is not one. Skipping."
			else:
				print "Invalid process identifier '" + options.process + "'"
				sys.exit(-1)
			continue

		# Iterate through the runs
		for run in runList:
			# Select the run
			print "Processing run #" + str(run)
			try:
				dataContainers[config].selectRun( run )
			except Exception as e:
				print str(e) + " Skipping."
				continue

			# Get the parameters of this run
			runAttributes = dataContainers[config].getRunAttributes()
			algorithm = runAttributes['networkType']
			if options.process == 'grid':
				laneCount = int(runAttributes['laneCount'])
				roadLength = int(runAttributes['roadLength'])
			elif options.process == 'highway':
				laneCount = int(runAttributes['laneCount'])
				roadLength = int(runAttributes['roadLength'])
				speed = float(runAttributes['speed'])
				nodeDensity = float(runAttributes['cpd'])
			elif options.process == 'location':
				location = runAttributes['launchCfg'].lstrip('xmldoc(\\"maps').rstrip('.launchd.xml\\")').lstrip('/')
			beaconInterval = float(runAttributes['beacon'])
			initialFreshness = float(runAttributes['initFreshness'])
			freshnessThreshold = float(runAttributes['freshThresh'])

			# Collect the results for this run.
			results = collectResults( dataContainers[config] )

			if options.process == 'grid':
				# The location data is specified by the lane count and the road length.
				if laneCount not in resultSet:
					resultSet[laneCount] = {}

				if roadLength not in resultSet[laneCount]:
					resultSet[laneCount][roadLength] = {}

				if algorithm not in resultSet[laneCount][roadLength]:
					resultSet[laneCount][roadLength][algorithm] = {}

				if beaconInterval not in resultSet[laneCount][roadLength][algorithm]:
					resultSet[laneCount][roadLength][algorithm][beaconInterval] = {}

				if initialFreshness not in resultSet[laneCount][roadLength][algorithm][beaconInterval]:
					resultSet[laneCount][roadLength][algorithm][beaconInterval][initialFreshness] = {}

				if freshnessThreshold not in resultSet[laneCount][roadLength][algorithm][beaconInterval][initialFreshness]:
					resultSet[laneCount][roadLength][algorithm][beaconInterval][initialFreshness][freshnessThreshold] = {}

				# Add them to the set
				for metric in results:
					# If this metric has not been added to this configuration, add it.
					if metric not in resultSet[laneCount][roadLength][algorithm][beaconInterval][initialFreshness][freshnessThreshold]:
						resultSet[laneCount][roadLength][algorithm][beaconInterval][initialFreshness][freshnessThreshold][metric] = []
					# Append the result to the list of metrics
					resultSet[laneCount][roadLength][algorithm][beaconInterval][initialFreshness][freshnessThreshold][metric].append( results[metric] )

			elif options.process == 'highway':
				# The location data is specified by the lane count, the road length, speed, and node density.
				if laneCount not in resultSet:
					resultSet[laneCount] = {}

				if roadLength not in resultSet[laneCount]:
					resultSet[laneCount][roadLength] = {}

				if speed not in resultSet[laneCount][roadLength]:
					resultSet[laneCount][roadLength][speed] = {}

				if nodeDensity not in resultSet[laneCount][roadLength][speed]:
					resultSet[laneCount][roadLength][speed][nodeDensity] = {}

				if algorithm not in resultSet[laneCount][roadLength][speed][nodeDensity]:
					resultSet[laneCount][roadLength][speed][nodeDensity][algorithm] = {}

				if beaconInterval not in resultSet[laneCount][roadLength][speed][nodeDensity][algorithm]:
					resultSet[laneCount][roadLength][speed][nodeDensity][algorithm][beaconInterval] = {}

				if initialFreshness not in resultSet[laneCount][roadLength][speed][nodeDensity][algorithm][beaconInterval]:
					resultSet[laneCount][roadLength][speed][nodeDensity][algorithm][beaconInterval][initialFreshness] = {}

				if freshnessThreshold not in resultSet[laneCount][roadLength][speed][nodeDensity][algorithm][beaconInterval][initialFreshness]:
					resultSet[laneCount][roadLength][speed][nodeDensity][algorithm][beaconInterval][initialFreshness][freshnessThreshold] = {}

				# Add them to the set
				for metric in results:
					# If this metric has not been added to this configuration, add it.
					if metric not in resultSet[laneCount][roadLength][speed][nodeDensity][algorithm][beaconInterval][initialFreshness][freshnessThreshold]:
						resultSet[laneCount][roadLength][speed][nodeDensity][algorithm][beaconInterval][initialFreshness][freshnessThreshold][metric] = []
					# Append the result to the list of metrics
					resultSet[laneCount][roadLength][speed][nodeDensity][algorithm][beaconInterval][initialFreshness][freshnessThreshold][metric].append( results[metric] )

			elif options.process == 'location':
				# The location data is specified by the name of the map
				if location not in resultSet:
					resultSet[location] = {}

				if roadLength not in resultSet[location]:
					resultSet[location] = {}

				if algorithm not in resultSet[location]:
					resultSet[location][algorithm] = {}

				if beaconInterval not in resultSet[location][algorithm]:
					resultSet[location][algorithm][beaconInterval] = {}

				if initialFreshness not in resultSet[location][algorithm][beaconInterval]:
					resultSet[location][algorithm][beaconInterval][initialFreshness] = {}

				if freshnessThreshold not in resultSet[location][algorithm][beaconInterval][initialFreshness]:
					resultSet[location][algorithm][beaconInterval][initialFreshness][freshnessThreshold] = {}

				# Add them to the set
				for metric in results:
					# If this metric has not been added to this configuration, add it.
					if metric not in resultSet[location][algorithm][beaconInterval][initialFreshness][freshnessThreshold]:
						resultSet[location][algorithm][beaconInterval][initialFreshness][freshnessThreshold][metric] = []
					# Append the result to the list of metrics
					resultSet[location][algorithm][beaconInterval][initialFreshness][freshnessThreshold][metric].append( results[metric] )

		# We've collected all the results, now we need to compute their means and stddevs
		if options.process == 'grid':
			for laneCount in resultSet:
				for roadLength in resultSet[laneCount]:
					for algorithm in resultSet[laneCount][roadLength]:
						for beaconInterval in resultSet[laneCount][roadLength][algorithm]:
							for initialFreshness in resultSet[laneCount][roadLength][algorithm][beaconInterval]:
								for freshnessThreshold in resultSet[laneCount][roadLength][algorithm][beaconInterval][initialFreshness]:
									for metric in resultSet[laneCount][roadLength][algorithm][beaconInterval][initialFreshness][freshnessThreshold]:
										metricMean = numpy.mean( resultSet[laneCount][roadLength][algorithm][beaconInterval][initialFreshness][freshnessThreshold][metric] )
										metricStd  =  numpy.std( resultSet[laneCount][roadLength][algorithm][beaconInterval][initialFreshness][freshnessThreshold][metric] )
										metricMax  =  numpy.max( resultSet[laneCount][roadLength][algorithm][beaconInterval][initialFreshness][freshnessThreshold][metric] )
										metricMin  =  numpy.min( resultSet[laneCount][roadLength][algorithm][beaconInterval][initialFreshness][freshnessThreshold][metric] )
										resultSet[laneCount][roadLength][algorithm][beaconInterval][initialFreshness][freshnessThreshold][metric] = (metricMean, metricStd, metricMax, metricMin)

		elif options.process == 'highway':
			for laneCount in resultSet:
				for roadLength in resultSet[laneCount]:
					for speed in resultSet[laneCount][roadLength]:
						for nodeDensity in resultSet[laneCount][roadLength][speed]:
							for algorithm in resultSet[laneCount][roadLength][speed][nodeDensity]:
								for beaconInterval in resultSet[laneCount][roadLength][speed][nodeDensity][algorithm]:
									for initialFreshness in resultSet[laneCount][roadLength][speed][nodeDensity][algorithm][beaconInterval]:
										for freshnessThreshold in resultSet[laneCount][roadLength][speed][nodeDensity][algorithm][beaconInterval][initialFreshness]:
											for metric in resultSet[laneCount][roadLength][speed][nodeDensity][algorithm][beaconInterval][initialFreshness][freshnessThreshold]:
												metricMean = numpy.mean( resultSet[laneCount][roadLength][speed][nodeDensity][algorithm][beaconInterval][initialFreshness][freshnessThreshold][metric] )
												metricStd  =  numpy.std( resultSet[laneCount][roadLength][speed][nodeDensity][algorithm][beaconInterval][initialFreshness][freshnessThreshold][metric] )
												metricMax  =  numpy.max( resultSet[laneCount][roadLength][speed][nodeDensity][algorithm][beaconInterval][initialFreshness][freshnessThreshold][metric] )
												metricMin  =  numpy.min( resultSet[laneCount][roadLength][speed][nodeDensity][algorithm][beaconInterval][initialFreshness][freshnessThreshold][metric] )
												resultSet[laneCount][roadLength][speed][nodeDensity][algorithm][beaconInterval][initialFreshness][freshnessThreshold][metric] = (metricMean, metricStd, metricMax, metricMin)

		elif options.process == 'location':
			for location in resultSet:
				for algorithm in resultSet[location]:
					for beaconInterval in resultSet[location][algorithm]:
						for initialFreshness in resultSet[location][algorithm][beaconInterval]:
							for freshnessThreshold in resultSet[location][algorithm][beaconInterval][initialFreshness]:
								for metric in resultSet[location][algorithm][beaconInterval][initialFreshness][freshnessThreshold]:
									metricMean = numpy.mean( resultSet[location][algorithm][beaconInterval][initialFreshness][freshnessThreshold][metric] )
									metricStd  =  numpy.std( resultSet[location][algorithm][beaconInterval][initialFreshness][freshnessThreshold][metric] )
									metricMax  =  numpy.max( resultSet[location][algorithm][beaconInterval][initialFreshness][freshnessThreshold][metric] )
									metricMin  =  numpy.min( resultSet[location][algorithm][beaconInterval][initialFreshness][freshnessThreshold][metric] )
									resultSet[location][algorithm][beaconInterval][initialFreshness][freshnessThreshold][metric] = (metricMean, metricStd, metricMax, metricMin)

	resultSet['settings'] = {}
	resultSet['settings']['process'] = options.process
	if options.process == 'grid':
		resultSet['settings']['precidence'] = ['Lane Count','Road Length','Algorithm','Beacon Interval','Initial Freshness','Freshness Threshold']
	elif options.process == 'highway':
		resultSet['settings']['precidence'] = ['Lane Count','Road Length','Speed','Node Density','Algorithm','Beacon Interval','Initial Freshness','Freshness Threshold']
	elif options.process == 'location':
		resultSet['settings']['precidence'] = ['Location','Algorithm','Beacon Interval','Initial Freshness','Freshness Threshold']

	# We've collated all the results. Attempt to dump this to a file.
	with open(options.outputFile,"w") as f:
		pickle.dump( resultSet, f )

	# Compute the elapsed time.
	elapsedTime = time.clock() - startTime
	hours = int( elapsedTime / 3600 )
	minutes = int( ( elapsedTime - hours*3600 ) / 60 )
	seconds = elapsedTime - hours*3600 - minutes*60
	print "Analysis time: " + str( hours ) + ":" + str( minutes ) + ":" + str( seconds )

#######################
# END OF DATA COMPILE #
#######################



#################
# Data analysis #
#################


metricPresentation = { "overhead" : "Overhead", "helloOverhead" : "Hello Overhead", "clusterLifetime" : "Cluster Lifetime" , "clusterSize" : "Cluster Size", "headChange" : "Head Change" }
metricUnits = { "overhead" : "B", "helloOverhead" : "B", "clusterLifetime" : "s", "clusterSize" : None, "headChange" : None }
metricMultiplier = { "overhead" : 1, "helloOverhead" : 1, "clusterLifetime" : 1 , "clusterSize" : 1, "headChange" : 1 }

# Parse the options passed to the data analyser.
def parseDataAnalyseOptions( argv ):
	optParser = OptionParser()
	optParser.add_option("-f", "--dataFile", dest="dataFile", help="Specify the compiled data file to load.")
	(options, args) = optParser.parse_args(argv)

	if not options.dataFile:
		print "Please specify the compiled datafile to load."
		optParser.print_help()
		sys.exit()
	return options

# Get a selection
def doSelection( message, selections ):
	print message
	for i in range(0,len(selections)):
		print str(i) + ".) " + str( selections[i] )
	print str(len(selections)) + ".) Quit"

	sels = []
	while len(sels) == 0:
		sels = raw_input( "Select (comma-separated for multiple choice): " ).split(",")

	if str(len(selections)) == sels[0]:
		return None

	retSel = []
	for s in sels:
		iSel = int(s)
		if iSel < 0 or iSel >= len(selections):
			continue
		retSel.append(selections[iSel])

	if len(retSel) == 0:
		return None

	return retSel



def locationAnalyse( resultData ):

	if 'settings' in resultData.keys():
		precidence = resultData['settings']['precidence']
		resultData.pop('settings',None)

	while True:
		# Pick a location (NOTE THIS ASSUMES THE LOCATIONS ARE THE SAME REGARDLESS OF THE ALGORITHM IN USE!)
		location = []
		while len(location) != 1:
			location = doSelection( "Locations (Choose one!):", resultData.keys() )
			if not location:
				break
		location = location[0]

		# Pick an algorithm
		algorithm = doSelection( "Algorithms:", resultData[location].keys() )
		if not algorithm:
			break

		# Now we go through the parameters one by one.
		# We pick the value we want to use at each point, but at least one of the variables 
		# needs to be selected as an x-axis variable.
		xAxis = None

		# Start with beacon interval
		beaconIntervals = sorted( resultData[location][algorithm[0]].keys(), key=lambda x: float(x) )
		beaconInterval = doSelection( "Beacon Interval:", beaconIntervals + ["Use as x-axis"] )
		if not beaconInterval:
			break
		beaconInterval = beaconInterval[0]
		if "Use as x-axis" == beaconInterval:
			xAxis = "Beacon Interval"
			beaconInterval = beaconIntervals[0]

		# Now go onto Initial Freshness
		initFreshnesses = sorted( resultData[location][algorithm[0]][beaconInterval].keys(), key=lambda x: float(x) )
		initialFreshness = None
		while True:
			initialFreshness = doSelection( "Initial Freshness:", initFreshnesses + ["Use as x-axis"] )
			if not initialFreshness:
				break
			initialFreshness = initialFreshness[0]
			if "Use as x-axis" == initialFreshness:
				if not xAxis:
					xAxis = "Initial Freshness"
					initialFreshness = initFreshnesses[0]
				else:
					print xAxis + " already chosen as x-axis."
					continue
			break
		if not initialFreshness:
			break

		# Now go to Freshness Threshold
		freshnessThresholds = sorted( resultData[location][algorithm[0]][beaconInterval][initialFreshness].keys(), key=lambda x: float(x) )
		freshnessThreshold = None
		while True:
			freshnessThreshold = doSelection( "Freshness Threshold:", freshnessThresholds + ["Use as x-axis"] )
			if not freshnessThreshold:
				break
			freshnessThreshold = freshnessThreshold[0]
			if "Use as x-axis" == freshnessThreshold:
				if not xAxis:
					xAxis = "Freshness Threshold"
					freshnessThreshold = freshnessThresholds[0]
				else:
					print xAxis + " already chosen as x-axis."
					continue
			break
		if not freshnessThreshold:
			break

		if not xAxis:
			print "You didn't choose an x-axis variable! Start again!"
			continue

		while True:
			# Now pick a metric:
			metrics = None
			metrics = doSelection( "Metric:", sorted(resultData[location][algorithm[0]][beaconInterval][initialFreshness][freshnessThreshold].keys()) )
			if not metrics:
				break

			saveToFiles = len(metrics) > 1

			for metric in metrics:
				# Now collect the data!
				for al in algorithm:
					xAxisVals = []
					yAxisVals = []
					yAxisError = []
					if xAxis == "Beacon Interval":
						for v in sorted( resultData[location][al].keys(), key = lambda x: float(x) ):
							xAxisVals.append( float(v) )
							yAxisVals.append( resultData[location][al][v][initialFreshness][freshnessThreshold][metric][0] )
							yAxisError.append( resultData[location][al][v][initialFreshness][freshnessThreshold][metric][1] )
					elif xAxis == "Initial Freshness":
						for v in sorted( resultData[location][al][beaconInterval].keys(), key = lambda x: float(x) ):
							xAxisVals.append( float(v) )
							yAxisVals.append( resultData[location][al][beaconInterval][v][freshnessThreshold][metric][0] )
							yAxisError.append( resultData[location][al][beaconInterval][v][freshnessThreshold][metric][1] )
					elif xAxis == "Freshness Threshold":
						for v in sorted( resultData[location][al][beaconInterval][initialFreshness].keys(), key = lambda x: float(x) ):
							xAxisVals.append( float(v) )
							yAxisVals.append( resultData[location][al][beaconInterval][initialFreshness][v][metric][0] )
							yAxisError.append( resultData[location][al][beaconInterval][initialFreshness][v][metric][1] )

					pyplot.errorbar( xAxisVals, yAxisVals, yerr = yAxisError, label=al )
					pyplot.xlim( [0,max(xAxisVals)+0.1] )

				pyplot.legend(loc='best')
				pyplot.xlabel(xAxis + " (s)")
				yl = metricPresentation[metric]
				if metricUnits[metric]:
					yl += " (" + metricUnits[metric] + ")"
				pyplot.ylabel(yl)

				titleStr = metricPresentation[metric] + " vs. " + xAxis
				titleStr += "\n(" + location + "; "
				if xAxis == "Beacon Interval":
					titleStr += "Initial Freshness: " + str(initialFreshness) + "; Freshness Threshold: " + str(freshnessThreshold) + ")"
				elif xAxis == "Initial Freshness":
					titleStr += "Beacon Interval: " + str(beaconInterval) + "; Freshness Threshold: " + str(freshnessThreshold) + ")"
				elif xAxis == "Freshness Threshold":
					titleStr += "Beacon Interval: " + str(beaconInterval) + "; Threshold Initial: " + str(initialFreshness) + ")"

				pyplot.title( titleStr )

				if saveToFiles:
					titleStr = titleStr.split("\n")
					pyplot.savefig( titleStr[0] + " " + titleStr[1] + ".pdf" )
					pyplot.close()

				else:
					pyplot.show()
					if raw_input( "Do you want to try a different metric? (Y/n) " ).lower() == 'n':
						break



def gridAnalyse(resultData):
	if 'settings' in resultData.keys():
		precidence = resultData['settings']['precidence']
		resultData.pop('settings',None)

	def acquireData( dat, axis, precidence, **kwargs ):
		hVals = []
		vVals = []
		loc = precidence.index(axis)
		if loc == 0:
			hVals = sorted( dat.keys() )
			vVals = [ dat[val][kwargs[precidence[1]]][kwargs[precidence[2]]][kwargs[precidence[3]]][kwargs[precidence[4]]][kwargs[precidence[5]]][kwargs['metric']] for val in hVals ]
		elif loc == 1:
			hVals = sorted( dat[kwargs[precidence[0]]].keys() )
			vVals = [ dat[kwargs[precidence[0]]][val][kwargs[precidence[2]]][kwargs[precidence[3]]][kwargs[precidence[4]]][kwargs[precidence[5]]][kwargs['metric']] for val in hVals ]
		elif loc == 2:
			hVals = sorted( dat[kwargs[precidence[0]]][kwargs[precidence[1]]].keys() )
			vVals = [ dat[kwargs[precidence[0]]][kwargs[precidence[1]]][val][kwargs[precidence[3]]][kwargs[precidence[4]]][kwargs[precidence[5]]][kwargs['metric']] for val in hVals ]
		elif loc == 3:
			hVals = sorted( dat[kwargs[precidence[0]]][kwargs[precidence[1]]][kwargs[precidence[2]]].keys() )
			vVals = [ dat[kwargs[precidence[0]]][kwargs[precidence[1]]][kwargs[precidence[2]]][val][kwargs[precidence[4]]][kwargs[precidence[5]]][kwargs['metric']] for val in hVals ]
		elif loc == 4:
			hVals = sorted( dat[kwargs[precidence[0]]][kwargs[precidence[1]]][kwargs[precidence[2]]][kwargs[precidence[3]]].keys() )
			vVals = [ dat[kwargs[precidence[0]]][kwargs[precidence[1]]][kwargs[precidence[2]]][kwargs[precidence[3]]][val][kwargs[precidence[5]]][kwargs['metric']] for val in hVals ]
		elif loc == 5:
			hVals = sorted( dat[kwargs[precidence[0]]][kwargs[precidence[1]]][kwargs[precidence[2]]][kwargs[precidence[3]]][kwargs[precidence[4]]].keys() )
			vVals = [ dat[kwargs[precidence[0]]][kwargs[precidence[1]]][kwargs[precidence[2]]][kwargs[precidence[3]]][kwargs[precidence[4]]][val][kwargs['metric']] for val in hVals ]
		return (hVals,vVals)

	markers = list(lines.Line2D.markers.keys())[5:] 

	while True:
		# Analyse grid 
		res = {}
		setAxes = []

		endThis = False
		xAxis = None
		currVals = resultData
		for n in precidence:
			res[n] = doSelection( 'Select a ' + n + ':', sorted(currVals.keys()) + ['Use as axis'] )
			if not res[n]:
				endThis = True
				break
			nextN = res[n][0]
			if nextN == 'Use as axis':
				res[n] = sorted(currVals.keys())
			nextN = res[n][0]
			if len(res[n]) > 1:
				setAxes.append(n)
				nextN = res[n][0]
				strMsg = "Use as horizontal axis"
				if xAxis:
					strMsg += " (Current: " + xAxis + ")"
				strMsg += "?"
				if raw_input( strMsg ).lower() == "y":
					xAxis = n
			currVals = currVals[nextN]

		if endThis:
			break

		permutations = list( itertools.product(*[ res[n] for n in precidence if n is not xAxis ]) )
		kwargSet = [dict( zip( [a for a in precidence if a is not xAxis], p ) ) for p in permutations]

		while True:
			# Now pick a metric:
			metrics = None
			metrics = doSelection( "Metric:", sorted(currVals) )
			if not metrics:
				break

			markerIndex = 0
			# Get the data
			for kw in kwargSet:
				conf = kw;
				conf['metric'] = metrics[0]
				hVals,vVals = acquireData( resultData, xAxis, precidence, **conf )
				labelStr = ""
				s = False
				for k in kw:
					if k in setAxes and k is not xAxis:
						if s:
							labelStr += "; "
						labelStr += k + "=" + str(kw[k])
						s = True
				v = [val[0] for val in vVals]
				pyplot.plot( hVals, v, label=labelStr, marker=markers[markerIndex] )
				markerIndex+=1
				if markerIndex == len(markers):
					markerIndex = 0
			pyplot.legend(loc='best')
			pyplot.title( metrics[0] + " vs. " + xAxis )
			pyplot.xlabel( xAxis )
			pyplot.ylabel( metrics[0] )
			pyplot.show()



def highwayAnalyse(resultData):
	if 'settings' in resultData.keys():
		precidence = resultData['settings']['precidence']
		resultData.pop('settings',None)

	def acquireData( dat, axis, precidence, **kwargs ):
		hVals = []
		vVals = []
		loc = precidence.index(axis)
		if loc == 0:
			hVals = sorted( dat.keys() )
			vVals = [ dat[val][kwargs[precidence[1]]][kwargs[precidence[2]]][kwargs[precidence[3]]][kwargs[precidence[4]]][kwargs[precidence[5]]][kwargs[precidence[6]]][kwargs[precidence[7]]][kwargs['metric']] for val in hVals ]
		elif loc == 1:
			hVals = sorted( dat[kwargs[precidence[0]]].keys() )
			vVals = [ dat[kwargs[precidence[0]]][val][kwargs[precidence[2]]][kwargs[precidence[3]]][kwargs[precidence[4]]][kwargs[precidence[5]]][kwargs[precidence[6]]][kwargs[precidence[7]]][kwargs['metric']] for val in hVals ]
		elif loc == 2:
			hVals = sorted( dat[kwargs[precidence[0]]][kwargs[precidence[1]]].keys() )
			vVals = [ dat[kwargs[precidence[0]]][kwargs[precidence[1]]][val][kwargs[precidence[3]]][kwargs[precidence[4]]][kwargs[precidence[5]]][kwargs[precidence[6]]][kwargs[precidence[7]]][kwargs['metric']] for val in hVals ]
		elif loc == 3:
			hVals = sorted( dat[kwargs[precidence[0]]][kwargs[precidence[1]]][kwargs[precidence[2]]].keys() )
			vVals = [ dat[kwargs[precidence[0]]][kwargs[precidence[1]]][kwargs[precidence[2]]][val][kwargs[precidence[4]]][kwargs[precidence[5]]][kwargs[precidence[6]]][kwargs[precidence[7]]][kwargs['metric']] for val in hVals ]
		elif loc == 4:
			hVals = sorted( dat[kwargs[precidence[0]]][kwargs[precidence[1]]][kwargs[precidence[2]]][kwargs[precidence[3]]].keys() )
			vVals = [ dat[kwargs[precidence[0]]][kwargs[precidence[1]]][kwargs[precidence[2]]][kwargs[precidence[3]]][val][kwargs[precidence[5]]][kwargs[precidence[6]]][kwargs[precidence[7]]][kwargs['metric']] for val in hVals ]
		elif loc == 5:
			hVals = sorted( dat[kwargs[precidence[0]]][kwargs[precidence[1]]][kwargs[precidence[2]]][kwargs[precidence[3]]][kwargs[precidence[4]]].keys() )
			vVals = [ dat[kwargs[precidence[0]]][kwargs[precidence[1]]][kwargs[precidence[2]]][kwargs[precidence[3]]][kwargs[precidence[4]]][val][kwargs[precidence[6]]][kwargs[precidence[7]]][kwargs['metric']] for val in hVals ]
		elif loc == 6:
			hVals = sorted( dat[kwargs[precidence[0]]][kwargs[precidence[1]]][kwargs[precidence[2]]][kwargs[precidence[3]]][kwargs[precidence[4]]][kwargs[precidence[6]]].keys() )
			vVals = [ dat[kwargs[precidence[0]]][kwargs[precidence[1]]][kwargs[precidence[2]]][kwargs[precidence[3]]][kwargs[precidence[4]]][kwargs[precidence[5]]][val][kwargs[precidence[7]]][kwargs['metric']] for val in hVals ]
		elif loc == 7:
			hVals = sorted( dat[kwargs[precidence[0]]][kwargs[precidence[1]]][kwargs[precidence[2]]][kwargs[precidence[3]]][kwargs[precidence[4]]][kwargs[precidence[6]]][kwargs[precidence[7]]].keys() )
			vVals = [ dat[kwargs[precidence[0]]][kwargs[precidence[1]]][kwargs[precidence[2]]][kwargs[precidence[3]]][kwargs[precidence[4]]][kwargs[precidence[5]]][kwargs[precidence[6]]][val][kwargs['metric']] for val in hVals ]
		return (hVals,vVals)

	markers = list(lines.Line2D.markers.keys())[5:] 

	while True:
		# Analyse grid 
		res = {}
		setAxes = []

		endThis = False
		xAxis = None
		currVals = resultData
		for n in precidence:
			res[n] = doSelection( 'Select a ' + n + ':', sorted(currVals.keys()) + ['Use as axis'] )
			if not res[n]:
				endThis = True
				break
			nextN = res[n][0]
			if nextN == 'Use as axis':
				res[n] = sorted(currVals.keys())
			nextN = res[n][0]
			if len(res[n]) > 1:
				setAxes.append(n)
				nextN = res[n][0]
				strMsg = "Use as horizontal axis"
				if xAxis:
					strMsg += " (Current: " + xAxis + ")"
				strMsg += "?"
				if raw_input( strMsg ).lower() == "y":
					xAxis = n
			currVals = currVals[nextN]

		if endThis:
			break

		permutations = list( itertools.product(*[ res[n] for n in precidence if n is not xAxis ]) )
		kwargSet = [dict( zip( [a for a in precidence if a is not xAxis], p ) ) for p in permutations]

		while True:
			# Now pick a metric:
			metrics = None
			metrics = doSelection( "Metric:", sorted(currVals) )
			if not metrics:
				break

			markerIndex = 0
			# Get the data
			for kw in kwargSet:
				conf = kw;
				conf['metric'] = metrics[0]
				try:
					hVals,vVals = acquireData( resultData, xAxis, precidence, **conf )
				except KeyError:
					print "Skipping " + str(conf)
					continue
				labelStr = ""
				s = False
				for k in kw:
					if k in setAxes and k is not xAxis:
						if s:
							labelStr += "; "
						labelStr += k + "=" + str(kw[k])
						s = True
				v = [val[0] for val in vVals]
				pyplot.plot( hVals, v, label=labelStr, marker=markers[markerIndex] )
				markerIndex+=1
				if markerIndex == len(markers):
					markerIndex = 0
			pyplot.legend(loc='best')
			pyplot.title( metrics[0] + " vs. " + xAxis )
			pyplot.xlabel( xAxis )
			pyplot.ylabel( metrics[0] )
			pyplot.show()


# Analyse the data
def dataAnalyse( argv ):
	options = parseDataAnalyseOptions( argv )

	try:
		with open( options.dataFile, "r" ) as f:
			resultData = pickle.load( f )
	except Exception as e:
		print str(e)
		sys.exit(0)

	if 'settings' in resultData.keys():
		process = resultData['settings']['process']
	else:
		process = 'location'

	# We have the data, now let's get to analysing it.
	if process == 'grid':
		gridAnalyse(resultData)
	elif process == 'highway':
		highwayAnalyse(resultData)
	elif process == 'location':
		locationAnalyse(resultData)
	else:
		print "Unknown process identifier '" + process + "'"

	print "Good bye!"

########################
# End of Data analysis #
########################




if __name__ == "__main__":
	if sys.argv[1] == "compile":
		dataCompile( sys.argv[2:] )
	elif sys.argv[1] == "analyse":
		dataAnalyse( sys.argv[2:] )
	else:
		print "Unknown command: " + sys.argv[1]


