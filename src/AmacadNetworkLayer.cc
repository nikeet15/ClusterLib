//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
// 

#include <cassert>
#include <algorithm>

#include "NetwControlInfo.h"
#include "BaseMacLayer.h"
#include "AddressingInterface.h"
#include "SimpleAddress.h"
#include "FindModule.h"
#include "ClusterAnalysisScenarioManager.h"
#include "AmacadNetworkLayer.h"
#include "AmacadControlMessage_m.h"
#include "ArpInterface.h"
#include "NetwToMacControlInfo.h"
#include "BaseMobility.h"
#include "BaseConnectionManager.h"
#include "ChannelAccess.h"

#include "TraCIScenarioManager.h"
#include "TraCIMobility.h"


Define_Module(AmacadNetworkLayer);


AmacadNetworkLayer::AmacadNetworkLayer() {
	// TODO Auto-generated constructor stub

}

AmacadNetworkLayer::~AmacadNetworkLayer() {
	// TODO Auto-generated destructor stub
}


int AmacadNetworkLayer::GetStateCount() {
	return ClusterHead+1;
}


int AmacadNetworkLayer::GetClusterState() {
	return mCurrentState;
}

bool AmacadNetworkLayer::IsClusterHead() {
	return mIsClusterHead;
}

bool AmacadNetworkLayer::IsSubclusterHead() {
	return false;
}

bool AmacadNetworkLayer::IsHierarchical() {
	return false;
}

int AmacadNetworkLayer::GetMinimumClusterSize() {
	return -1;
}

void AmacadNetworkLayer::UpdateMessageString() {

	std::string msg;
	switch ( mCurrentState ) {

	case        Unclustered: msg =        "Unclustered"; break;
	//case NeighbourDiscovery: msg = "NeighbourDiscovery"; break;
	case            Joining: msg =            "Joining"; break;
	case      ClusterMember: msg =      "ClusterMember"; break;
	case        ClusterHead: msg =        "ClusterHead"; break;
	case       Reclustering: msg =       "Reclustering"; break;
	                default: msg =          "UNKNOWN!!"; break;

	};

	std::stringstream s;
	s << "N" << mID << ": " << msg;
	mMessageString = s.str();

}

/** @brief Initialization of the module and some variables*/
void AmacadNetworkLayer::initialize( int stage ) {

	ClusterAlgorithm::initialize(stage);

    if(stage == 1) {

		mInitialised = false;

		// set up the node.
		mID = getId();
		mMobility = FindModule<BaseMobility*>::findSubModule(findHost());
		mClusterHead = -1;
		mIsClusterHead = false;
		mCurrentMaximumClusterSize = 0;
		mClusterStartTime = 0;
		mLastSpeed = mLastBandwidth = -1;
		mCurrentState = Unclustered;

		// load configurations
		mMinimumDensity = par("minimumDensity").longValue();
		mMaximumDensity = par("maximumDensity").longValue();
		mMaximumClusterDensity = par("maximumClusterDensity").longValue();
		mMaximumWarningCount = par("maximumWarningCount").longValue();
		mSpeedThreshold = par("speedThreshold").doubleValue();
		mBandwidthThreshold = par("bandwidthThreshold").doubleValue();
		mTimeDifference = par("timeDifference").doubleValue();
		mTimeToLive = par("timeToLive").doubleValue();
		mTimeoutPeriod = par("timeoutPeriod").doubleValue();

		mWeights[0] = par("distanceWeight").doubleValue();
		mWeights[1] = par("speedWeight").doubleValue();
		mWeights[2] = par("destinationWeight").doubleValue();


		// set up self-messages
		mStartMessage = new cMessage( "start" );
		mAffiliationTimeoutMessage = new cMessage( "affiliationTimeout" );
		mJoinTimeoutMessage = new cMessage( "joinTimeout" );
		mInquiryTimeoutMessage = new cMessage( "inquiryTimeout" );
		mChangeDestinationMessage = new cMessage( "changeDestination" );
		mUpdateMobilityMessage = new cMessage( "updateMobility" );

		// set up watches
		WATCH_SET( mClusterMembers );
		//WATCH_MAP( mNeighbours );
		WATCH( mClusterHead );

		// Load this car's destination list.
		GetDestinationList();

		// Schedule the change in destination.
		scheduleAt( simTime() + mDestinationSet[0].mTime, mChangeDestinationMessage );

		// Schedule the "update mobility" signal
		double diff = 1 + rand() / (double)RAND_MAX;
		scheduleAt( simTime() + mTimeDifference * diff, mStartMessage );

		// Set up the first destination.
		mCurrentDestination = mDestinationSet[0].mDestination;

		// Clear the current destination in anticipation of the next one.
		mDestinationSet.erase( mDestinationSet.begin() );

    }

}

/** @brief Cleanup*/
void AmacadNetworkLayer::finish() {

    if ( mAffiliationTimeoutMessage->isScheduled() )
        cancelEvent( mAffiliationTimeoutMessage );
    delete mAffiliationTimeoutMessage;

    if ( mJoinTimeoutMessage->isScheduled() )
        cancelEvent( mJoinTimeoutMessage );
    delete mJoinTimeoutMessage;

    if ( mInquiryTimeoutMessage->isScheduled() )
        cancelEvent( mInquiryTimeoutMessage );
    delete mInquiryTimeoutMessage;

    if ( mChangeDestinationMessage->isScheduled() )
        cancelEvent( mChangeDestinationMessage );
    delete mChangeDestinationMessage;

    if ( mUpdateMobilityMessage->isScheduled() )
        cancelEvent( mUpdateMobilityMessage );
    delete mUpdateMobilityMessage;


    ClusterAlgorithm::finish();

}




/** @brief Handle messages from upper layer */
void AmacadNetworkLayer::handleUpperMsg(cMessage* msg) {

    assert(dynamic_cast<cPacket*>(msg));
    NetwPkt *m = encapsMsg(static_cast<cPacket*>(msg));
    if ( m )
    	sendDown(m);

}


/** @brief Handle messages from lower layer */
void AmacadNetworkLayer::handleLowerMsg( cMessage* m ) {

	int id;
    AmacadControlMessage *msg = static_cast<AmacadControlMessage*>(m);
    UpdateNeighbour(msg);

    switch( m->getKind() ) {

        case AFFILIATION_MESSAGE:

        	//std::cerr << "Node " << mID << ": Received affiliation message from " << msg->getNodeId() << ".\n";

        	// We've received an affiliation message.
        	// Respond if we're a CH.
        	if ( mIsClusterHead ) {

        		SendControlMessage( AFFILIATION_ACK_MESSAGE, msg->getNodeId() );

        	}
        	break;

        case AFFILIATION_ACK_MESSAGE:

        	//std::cerr << "Node " << mID << ": Received affiliation ack message from " << msg->getNodeId() << ".\n";

        	// Got a response to an AFFILIATION_MESSAGE.
        	// Add this to our list of nearby CHs if it is one.
        	if ( mCurrentState == Unclustered && msg->getIsClusterHead() )
				mClusterHeadNeighbours.push_back( msg->getNodeId() );

        	break;

        case HELLO_MESSAGE:

        	//std::cerr << "Node " << mID << ": Received HELLO message from " << msg->getNodeId() << ".\n";

        	// Respond with an ACK.
        	SendControlMessage( HELLO_ACK_MESSAGE, msg->getNodeId() );

        	// HELLO messages come from CHs managing the clustering process.
        	if ( msg->getIsClusterHead() )
				mClusterHeadNeighbours.push_back( msg->getNodeId() );

        	break;

        case HELLO_ACK_MESSAGE:

        	// We got an ACK from a node.
        	// Need to do stuff if we are a CH
        	id = msg->getNodeId();
        	if ( mIsClusterHead ) {

        		// Check if it's a member of our cluster.
        		if ( mClusterMembers.find( id ) != mClusterMembers.end() ) {

        			// Node is in our cluster. Make sure it thinks we are it's CH.
        			if ( mNeighbours[id].mClusterHead != mID ) {

        				// Node is NOT part of our cluster so remove it.
        				std::cerr << "Node " << mID << ": Received HELLO ACK message from " << msg->getNodeId() << ", which we thought erroneously was a member of our cluster.\n";
        				ClusterMemberRemoved( id );
        				if ( mClusterMembers.size() < mMinimumDensity ) {
        					mCurrentState = Reclustering;
        					Process();
        				}

        			}

        		}

        	}

        	break;

        case ADD_MESSAGE:

        	// Received an ADD message.
        	if ( !mIsClusterHead )
        		break;

        	//std::cerr << "Node " << mID << ": Received ADD message from " << msg->getNodeId() << ".\n";

        	id = msg->getNodeId();
        	ClusterAnalysisScenarioManagerAccess::get()->joinMessageReceived( id, mID );

        	// We should check if this node is a better CH than us.
//        	if ( CalculateF() > CalculateF( id, true ) ) {
//
//        		// This node is better than us.
//        		ChangeCH(id);
//
//        	} else {

        		// We're still best.
        		if ( mClusterMembers.size() < mMaximumDensity ) {

        			// We can accommodate this node.
					ClusterMemberAdded( id );
					SendControlMessage( MEMBER_ACK_MESSAGE, id );
					std::cerr << "Node " << mID << ": Permitted node " << id << " to join cluster.\n";

        		} else {

        			// We cannot accommodate this node.
        			mClusterHead = -1;
					SendControlMessage( MEMBER_ACK_MESSAGE, id );
					mClusterHead = mID;
					std::cerr << "Node " << mID << ": Denied node " << id << " to join cluster.\n";

        		}

//        	}

        	break;

        case CLUSTERHEAD_ACK_MESSAGE:

        	// Received a command to become CH.
        	//std::cerr << "Node " << mID << ": Received CLUSTERHEAD ack message from " << msg->getNodeId() << ".\n";

        	mWarningMessageCount = 0;
        	mClusterHead = mID;
        	mIsClusterHead = true;
        	mCurrentState = ClusterHead;
        	mClusterMembers.clear();
        	for ( NeighbourEntrySet::iterator it = msg->getClusterTable().begin(); it != msg->getClusterTable().end(); it++ ) {

        		// Check if we have this neighbour already, and if we do, check if we have more recent data.
        		if ( mNeighbours.find(it->mID) == mNeighbours.end() || mNeighbours[it->mID].mLastHeard < it->mLastHeard ) {

        			// Either we done have the data, or we've been given a more recent entry.
        			mNeighbours[it->mID].mID = it->mID;
        			mNeighbours[it->mID].mNetworkAddress = it->mNetworkAddress;
        			mNeighbours[it->mID].mPosition.x = it->mPositionX;
        			mNeighbours[it->mID].mPosition.y = it->mPositionY;
        			mNeighbours[it->mID].mSpeed = it->mSpeed;
        			mNeighbours[it->mID].mDestination.x = it->mDestinationX;
        			mNeighbours[it->mID].mDestination.y = it->mDestinationY;
        			mNeighbours[it->mID].mIsClusterHead = it->mIsClusterHead;
        			mNeighbours[it->mID].mClusterHead = it->mClusterHead;
        			mNeighbours[it->mID].mNeighbourCount = it->mNeighbourCount;
        			mNeighbours[it->mID].mClusterSize = it->mClusterSize;
        			mNeighbours[it->mID].mValueF = CalculateF(it->mID);
        			mNeighbours[it->mID].mLastHeard = it->mLastHeard;

        		}

        		// Add this ID to the cluster members.
        		ClusterMemberAdded( it->mID );

        	}

        	// Add the ID of the old CH.
        	ClusterMemberAdded( msg->getNodeId() );

        	//std::cerr << "Node " << mID << ": Became CH. Node count = " << mClusterMembers.size() << "\n";
        	ClusterStarted();

        	// Check the state machine.
        	Process();

        	break;

        case MEMBER_ACK_MESSAGE:

        	std::cerr << "Node " << mID << ": Received MEMBER ack message from " << msg->getNodeId() << ".\n";

        	if ( mCurrentState != Joining )
        		break;

        	// Received a response to our ADD request.
        	cancelEvent( mJoinTimeoutMessage );

        	if ( msg->getClusterHead() != -1 ) {

        		std::cerr << "Node " << mID << ": Joined cluster " << msg->getNodeId() << ".\n";

        		// We've been accepted to this cluster
        		mCurrentState = ClusterMember;
				mClusterHead = msg->getNodeId();

	        	// Check the state machine.
	        	Process( NULL );

        	} else {

        		std::cerr << "Node " << mID << ": Denied access to cluster " << msg->getNodeId() << ".\n";

        		// We've been denied access to this cluster.
        		// Process the state machine with the JoinTimeoutMessage.
        		Process( mJoinTimeoutMessage );

        	}


        	break;

        case MEMBER_UPDATE_MESSAGE:

        	//std::cerr << "Node " << mID << ": Received MEMBER update message from " << msg->getNodeId() << ".\n";

        	// Received a notification that the cluster's CH has changed.
        	mClusterHead = msg->getClusterHead();

        	// Check if this frame has more up-to-date information about the new CH.
        	if ( mNeighbours.find(mClusterHead) == mNeighbours.end() || mNeighbours[mClusterHead].mLastHeard < msg->getClusterTable()[0].mLastHeard ) {

				// Either we done have the data, or we've been given a more recent entry.
				mNeighbours[mClusterHead].mID = mClusterHead;
				mNeighbours[mClusterHead].mNetworkAddress = msg->getClusterTable()[0].mNetworkAddress;
				mNeighbours[mClusterHead].mPosition.x = msg->getClusterTable()[0].mPositionX;
				mNeighbours[mClusterHead].mPosition.y = msg->getClusterTable()[0].mPositionY;
				mNeighbours[mClusterHead].mSpeed = msg->getClusterTable()[0].mSpeed;
				mNeighbours[mClusterHead].mDestination.x = msg->getClusterTable()[0].mDestinationX;
				mNeighbours[mClusterHead].mDestination.y = msg->getClusterTable()[0].mDestinationY;
				mNeighbours[mClusterHead].mIsClusterHead = msg->getClusterTable()[0].mIsClusterHead;
				mNeighbours[mClusterHead].mClusterHead = msg->getClusterTable()[0].mClusterHead;
				mNeighbours[mClusterHead].mNeighbourCount = msg->getClusterTable()[0].mNeighbourCount;
				mNeighbours[mClusterHead].mClusterSize = msg->getClusterTable()[0].mClusterSize;
				mNeighbours[mClusterHead].mValueF = CalculateF(mClusterHead);
				mNeighbours[mClusterHead].mLastHeard = msg->getClusterTable()[0].mLastHeard;

			}

        	break;

        case DELETE_MESSAGE:

        	//std::cerr << "Node " << mID << ": Received DELETE message from " << msg->getNodeId() << ".\n";

        	if ( mIsClusterHead ) {

            	// We've been told a node is leaving the cluster.
        		ClusterMemberRemoved( msg->getNodeId() );
        		if ( mClusterMembers.size() < mMinimumDensity ) {
        			mCurrentState = Reclustering;
        			Process();
        		}

        	} else {

        		// We've been told to leave the CH.
        		mClusterHead = -1;
        		mCurrentState = Unclustered;
        		Process();

        	}

        	break;

        case WARNING_MESSAGE:

        	//std::cerr << "Node " << mID << ": Received WARNING message from " << msg->getNodeId() << ".\n";

        	// Received a warning message from one of our CMs.
        	mWarningMessageCount++;
        	if ( mWarningMessageCount > mMaximumWarningCount ) {
        		std::cerr << "Node " << mID << ": TOO MANY WARNINGS! RECLUSTERING.\n";
        		ChangeCH();
        		Process();
        	}

        	break;

        case RECLUSTERING_MESSAGE:

        	//std::cerr << "Node " << mID << ": Received RECLUSTERING message from " << msg->getNodeId() << ".\n";

        	// A nearby CH is wanting to recluster.
        	if ( mIsClusterHead )
        		SendControlMessage( RECLUSTERING_ACK_MESSAGE, msg->getNodeId() );

        	break;

        case RECLUSTERING_ACK_MESSAGE:

        	//std::cerr << "Node " << mID << ": Received RECLUSTERING ack message from " << msg->getNodeId() << ".\n";

        	// We got an ACK to a reclustering message.
        	if ( mCurrentState == Reclustering ) {

        		// Combine our new neighbour table data
            	for ( NeighbourEntrySet::iterator it = msg->getClusterTable().begin(); it != msg->getClusterTable().end(); it++ ) {

            		// Check if we have this neighbour already, and if we do, check if we have more recent data.
            		if ( mNeighbours.find(it->mID) == mNeighbours.end() || mNeighbours[it->mID].mLastHeard < it->mLastHeard ) {

            			// Either we done have the data, or we've been given a more recent entry.
            			mNeighbours[it->mID].mID = it->mID;
            			mNeighbours[it->mID].mNetworkAddress = it->mNetworkAddress;
            			mNeighbours[it->mID].mPosition.x = it->mPositionX;
            			mNeighbours[it->mID].mPosition.y = it->mPositionY;
            			mNeighbours[it->mID].mSpeed = it->mSpeed;
            			mNeighbours[it->mID].mDestination.x = it->mDestinationX;
            			mNeighbours[it->mID].mDestination.y = it->mDestinationY;
            			mNeighbours[it->mID].mIsClusterHead = it->mIsClusterHead;
            			mNeighbours[it->mID].mClusterHead = it->mClusterHead;
            			mNeighbours[it->mID].mNeighbourCount = it->mNeighbourCount;
            			mNeighbours[it->mID].mClusterSize = it->mClusterSize;
            			mNeighbours[it->mID].mValueF = CalculateF(it->mID);
            			mNeighbours[it->mID].mLastHeard = it->mLastHeard;

            		}

            	}

        		// Run the process loop so we can send RECLUSTERING messages to the next CH.
        		cancelEvent( mJoinTimeoutMessage );
        		Process( mJoinTimeoutMessage );

        	}

        	break;

        case DATA:
        	sendUp( decapsMsg(msg) );
        	return;

    };

    delete m;

}


/** @brief Handle self messages */
void AmacadNetworkLayer::handleSelfMsg(cMessage* msg) {

	UpdateMessageString();

	if ( mStartMessage == msg ) {

		// Time to start!
		mInitialised = true;
		// Schedule the "update mobility" signal
		scheduleAt( simTime() + mTimeDifference, mUpdateMobilityMessage );
		Process(NULL);

	} else if ( mChangeDestinationMessage == msg ) {

		// We have to change the destination of our vehicle.
		if ( mDestinationSet.empty() )
			return;

		// Trigger the change in destination.
		scheduleAt( simTime() + mDestinationSet[0].mTime, mChangeDestinationMessage );

		// Set up the first destination.
		mCurrentDestination = mDestinationSet[0].mDestination;

		// Clear the current destination in anticipation of the next one.
		mDestinationSet.erase( mDestinationSet.begin() );
		return;

	} else {

		Process(msg);

	}

}


/** @brief decapsulate higher layer message from RmacControlMessage */
cMessage* AmacadNetworkLayer::decapsMsg( AmacadControlMessage *msg ) {

    cMessage *m = msg->decapsulate();
    setUpControlInfo(m, msg->getSrcAddr());

    // delete the netw packet
    delete msg;
    return m;

}



/** @brief Encapsulate higher layer packet into a RmacControlMessage */
NetwPkt* AmacadNetworkLayer::encapsMsg( cPacket *appPkt ) {


    LAddress::L2Type macAddr;
    LAddress::L3Type netwAddr;

    coreEV <<"in encaps...\n";

    AmacadControlMessage *pkt = new AmacadControlMessage(appPkt->getName(), DATA);

    pkt->setBitLength(headerLength);    // ordinary IP packet

    cObject* cInfo = appPkt->removeControlInfo();

    if(cInfo == NULL){
        EV << "warning: Application layer did not specifiy a destination L3 address\n"
           << "\tusing broadcast address instead\n";
        netwAddr = LAddress::L3BROADCAST;
    } else {
        coreEV <<"CInfo removed, netw addr="<< NetwControlInfo::getAddressFromControlInfo( cInfo ) << std::endl;
            netwAddr = NetwControlInfo::getAddressFromControlInfo( cInfo );
        delete cInfo;
    }

    pkt->setSrcAddr(myNetwAddr);
    pkt->setDestAddr(netwAddr);
    coreEV << " netw "<< myNetwAddr << " sending packet" <<std::endl;
    if(LAddress::isL3Broadcast( netwAddr )) {
        coreEV << "sendDown: nHop=L3BROADCAST -> message has to be broadcasted"
           << " -> set destMac=L2BROADCAST\n";
        macAddr = LAddress::L2BROADCAST;
    }
    else{
        coreEV <<"sendDown: get the MAC address\n";
        if ( simulation.getModule( static_cast<int>(netwAddr ) ) ) {
        	macAddr = arp->getMacAddr(netwAddr);
        } else {
        	coreEV << "sendDown: Cannot find a module with the network address: " << netwAddr << std::endl;
        	return NULL;
        }
    }

    setDownControlInfo(pkt, macAddr);

    //encapsulate the application packet
    pkt->encapsulate(appPkt);
    coreEV <<" pkt encapsulated\n";
    return (NetwPkt*)pkt;

}




/**
 * Process the algorithm state machine.
 */
void AmacadNetworkLayer::Process( cMessage *msg ) {

	if ( msg == mUpdateMobilityMessage )
		scheduleAt( simTime() + mTimeDifference, mUpdateMobilityMessage );

	bool processComplete = false;

	while ( !processComplete ) {

		processComplete = true;
		switch( mCurrentState ) {

			case Unclustered:

				// The node is in the unclustered state.
				if ( !msg ) {

					// Look for CH neighbours.
					CollectClusterHeadNeighbours();
					if ( mClusterHeadNeighbours.empty() ) {

						// Send out affiliation message.
						std::cerr << "Node " << mID << ": Sending out AFFILIATION MESSAGE.\n";
						SendControlMessage( AFFILIATION_MESSAGE );
						scheduleAt( simTime() + mTimeoutPeriod, mAffiliationTimeoutMessage );

					} else {

						// We have CHs nearby. Ask to join them.
						mCurrentState = Joining;
						processComplete = false;
						msg = mJoinTimeoutMessage;

					}


				} else if ( msg == mAffiliationTimeoutMessage ) {

					std::cerr << "Node " << mID << ": Affiliation timeout.\n";
					// Check if we got any CHs.
					if ( !mClusterHeadNeighbours.empty() ) {

						// We have CHs nearby. Ask to join them.
						std::cerr << "Node " << mID << ": Found " << mClusterHeadNeighbours.size() << " CHs in range. Joining!\n";
						mCurrentState = Joining;
						msg = mJoinTimeoutMessage;

					} else {

						// No CHs nearby, so start the clustering process!
						std::cerr << "Node " << mID << ": No CHs in range.\n";
						mCurrentState = ClusterHead;
						mIsClusterHead = true;
						mClusterHead = mID;		// Set ourselves as CH.
						mWarningMessageCount = 0;
						// Send out a HELLO message to get things moving.
						SendControlMessage( HELLO_MESSAGE );
						msg = NULL;

					}
					processComplete = false;

				}

				break;

			case Joining:

				if ( msg == mJoinTimeoutMessage ) {

					if ( !mClusterHeadNeighbours.empty() ) {

						// Ask to join one of the CHs nearby.
		        		std::cerr << "Node " << mID << ": Requesting to join " << mClusterHeadNeighbours[0] << ".\n";

						SendControlMessage( ADD_MESSAGE, mClusterHeadNeighbours[0] );
						ClusterAnalysisScenarioManagerAccess::get()->joinMessageSent( mID, mClusterHeadNeighbours[0] );
						mClusterHeadNeighbours.erase( mClusterHeadNeighbours.begin() );
						scheduleAt( simTime() + mTimeoutPeriod, mJoinTimeoutMessage );

					} else {

						// Change the state to unclustered and look for more CHs
						std::cerr << "Node " << mID << ": No CHs responded! Going back to Unclustered.\n";
						mCurrentState = Unclustered;
						processComplete = false;
						msg = NULL;

					}

				}

				break;

			case ClusterMember:

				if ( msg == mUpdateMobilityMessage ) {

					// Check if our mobility has significantly changed.
					double currSpeed = mMobility->getCurrentSpeed().length();
					if ( fabs( currSpeed - mLastSpeed ) > mSpeedThreshold )
						SendControlMessage( WARNING_MESSAGE, mClusterHead );

					mLastSpeed = currSpeed;

					// Now check our neighbour table for outdated data
					bool lostCH = false;
					NodeIdList nodesToRemove;
					for ( NeighbourIterator it = mNeighbours.begin(); it != mNeighbours.end(); it++ ) {

						simtime_t diff = simTime() - it->second.mLastHeard;
						if ( diff > mTimeToLive ) {
							// Mark this node for deletion
							nodesToRemove.push_back( it->first );
							lostCH = lostCH || ( mClusterHead == it->first );
						}

					}

					// Delete marked nodes.
					for ( NodeIdList::iterator it = nodesToRemove.begin(); it != nodesToRemove.end(); it++ )
						mNeighbours.erase( *it );

					if ( lostCH ) {

						// We lost our CH, so find a new one.
			    		std::cerr << "Node " << mID << ": Lost CH " << mClusterHead << ".\n";

						mCurrentState = Unclustered;
						mClusterHead = -1;
						processComplete = false;

					}

				}

				break;

			case ClusterHead:

				if ( msg == mUpdateMobilityMessage ) {

					//std::cerr << "Node " << mID << ": ClusterHead update mobility message.\n";

					// Send out a HELLO beacon.
					SendControlMessage( HELLO_MESSAGE );

					// Decrement warning message counts.
					if ( (--mWarningMessageCount) < 0 )
						mWarningMessageCount = 0;

					// Now check our neighbour table for outdated data
					NodeIdList nodesToRemove;
					bool lostMembers = false;
					for ( NeighbourIterator it = mNeighbours.begin(); it != mNeighbours.end(); it++ ) {

						simtime_t diff = simTime() - it->second.mLastHeard;
						if ( diff > mTimeToLive ) {
							// Mark this node for deletion
							nodesToRemove.push_back( it->first );
							// We've lost contact with our CH, so look for a new one.
							if ( mClusterMembers.find( it->first ) != mClusterMembers.end() ) {
								mClusterMembers.erase( it->first );
								lostMembers = true;
							}
						}

					}

					// Delete marked nodes.
					for ( NodeIdList::iterator it = nodesToRemove.begin(); it != nodesToRemove.end(); it++ )
						mNeighbours.erase( *it );

					// Check if we've lost all our members.
					if ( lostMembers && mClusterMembers.size() < mMinimumDensity ) {

						// Perform reclustering.
						mCurrentState = Reclustering;
						processComplete = false;
						msg = NULL;
						continue;

					}


					// Check how many CHs are in range.
					CollectClusterHeadNeighbours();
					if ( !mClusterHeadNeighbours.empty() ) {

						// Check if the number of clusters is too high
						if ( mClusterHeadNeighbours.size() > mMaximumClusterDensity ) {

							// Perform reclustering
							mCurrentState = Reclustering;
							processComplete = false;
							msg = NULL;

						} else if ( mClusterMembers.empty() ) {

							// We don't have any members! Let's join another CH.
							mCurrentState = Unclustered;
							mIsClusterHead = false;
							mClusterHead = -1;
							processComplete = false;
							msg = NULL;

						}

					}

				}

				break;

			case Reclustering:

				if ( !msg ) {

					// Look for CH neighbours.
					CollectClusterHeadNeighbours();
					if ( mClusterHeadNeighbours.empty() ) {

						// We have no nearby CHs. Tell our remaining members to disconnect and restart clustering phase.
						for ( NodeIdSet::iterator it = mClusterMembers.begin(); it != mClusterMembers.end(); it++ )
							SendControlMessage( DELETE_MESSAGE, *it );

						// Return to the Unclustered state.
						mClusterMembers.clear();
						mClusterHead = -1;
						mCurrentState = Unclustered;
						mIsClusterHead = processComplete = false;
						//std::cerr << "Node " << mID << ": No nearby CHs! Going to unclustered state.\n";

					} else {

						// We have nearby CHs. Restart the loop, and trigger transmission of the RECLUSTERING message
						msg = mJoinTimeoutMessage;
						processComplete = false;
						//std::cerr << "Node " << mID << ": Reclustering.\n";

					}

				} else if ( msg == mJoinTimeoutMessage ) {

					if ( mClusterHeadNeighbours.empty() ) {

						// No more neighbours, so see whether we should change the CH.
						ChangeCH();

					} else {

						// We have to send a RECLUSTERING message to the first CH.
						SendControlMessage( RECLUSTERING_MESSAGE, mClusterHeadNeighbours[0] );
						mClusterHeadNeighbours.erase( mClusterHeadNeighbours.begin() );
						scheduleAt( simTime() + mTimeoutPeriod, mJoinTimeoutMessage );

					}

				}

				break;

		};

	};

}










/**
 * @brief Send a control message
 */
void AmacadNetworkLayer::SendControlMessage( int type, int destID ) {

    LAddress::L2Type macAddr;
    LAddress::L3Type netwAddr;

    if ( destID == -1 )
        netwAddr = LAddress::L3BROADCAST;
    else
        netwAddr = mNeighbours[destID].mNetworkAddress;

    AmacadControlMessage *pkt = new AmacadControlMessage( "cluster-ctrl", type );
    int s = 56;

    coreEV << "sending AMACAD ";
    if ( type == AFFILIATION_MESSAGE ) {
    	coreEV << "AFFILIATION_MESSAGE";
    } else if ( type == AFFILIATION_ACK_MESSAGE ) {
        coreEV << "AFFILIATION_ACK_MESSAGE";
    } else if ( type == HELLO_MESSAGE ) {
        coreEV << "HELLO_MESSAGE";
    } else if ( type == HELLO_ACK_MESSAGE ) {
        coreEV << "HELLO_ACK_MESSAGE";
    } else if ( type == ADD_MESSAGE ) {
        coreEV << "ADD_MESSAGE";
    } else if ( type == MEMBER_ACK_MESSAGE ) {
        coreEV << "MEMBER_ACK_MESSAGE";
    } else if ( type == CLUSTERHEAD_ACK_MESSAGE ) {

    	coreEV << "CLUSTERHEAD_ACK_MESSAGE";

    	// Add cluster members to the packet.
    	for ( NodeIdSet::iterator it = mClusterMembers.begin(); it != mClusterMembers.end(); it++ ) {

    		if ( *it == destID )
    			continue;

    		NeighbourEntry nEntry;
    		nEntry.mID = mNeighbours[*it].mID;
    		nEntry.mNetworkAddress = mNeighbours[*it].mNetworkAddress;
    		nEntry.mPositionX = mNeighbours[*it].mPosition.x;
    		nEntry.mPositionY = mNeighbours[*it].mPosition.y;
    		nEntry.mSpeed = mNeighbours[*it].mSpeed;
    		nEntry.mDestinationX = mNeighbours[*it].mDestination.x;
    		nEntry.mDestinationY = mNeighbours[*it].mDestination.y;
    		nEntry.mNeighbourCount = mNeighbours[*it].mNeighbourCount;
    		nEntry.mClusterHead = mNeighbours[*it].mClusterHead;
    		nEntry.mIsClusterHead = mNeighbours[*it].mIsClusterHead;
    		nEntry.mClusterSize = mNeighbours[*it].mClusterSize;
    		nEntry.mLastHeard = mNeighbours[*it].mLastHeard;
    		pkt->getClusterTable().push_back( nEntry );
    		s += 56;

    	}

    } else if ( type == MEMBER_UPDATE_MESSAGE ) {

    	coreEV << "MEMBER_UPDATE_MESSAGE";

    	// Include data about the new CH.
		NeighbourEntry nEntry;
		nEntry.mID = mNeighbours[mClusterHead].mID;
		nEntry.mNetworkAddress = mNeighbours[mClusterHead].mNetworkAddress;
		nEntry.mPositionX = mNeighbours[mClusterHead].mPosition.x;
		nEntry.mPositionY = mNeighbours[mClusterHead].mPosition.y;
		nEntry.mSpeed = mNeighbours[mClusterHead].mSpeed;
		nEntry.mDestinationX = mNeighbours[mClusterHead].mDestination.x;
		nEntry.mDestinationY = mNeighbours[mClusterHead].mDestination.y;
		nEntry.mNeighbourCount = mNeighbours[mClusterHead].mNeighbourCount;
		nEntry.mClusterHead = mNeighbours[mClusterHead].mClusterHead;
		nEntry.mIsClusterHead = mNeighbours[mClusterHead].mIsClusterHead;
		nEntry.mClusterSize = mNeighbours[mClusterHead].mClusterSize;
		nEntry.mLastHeard = mNeighbours[mClusterHead].mLastHeard;
		pkt->getClusterTable().push_back( nEntry );
		s += 56;

    } else if ( type == DELETE_MESSAGE ) {
        coreEV << "DELETE_MESSAGE";
    } else if ( type == WARNING_MESSAGE ) {
        coreEV << "WARNING_MESSAGE";
    } else if ( type == RECLUSTERING_MESSAGE ) {
        coreEV << "RECLUSTERING_MESSAGE";
    } else if ( type == RECLUSTERING_ACK_MESSAGE ) {
        coreEV << "RECLUSTERING_ACK_MESSAGE";
    }

    coreEV << " message...\n";

    pkt->setBitLength(s); // size of the control packet packet.

    if ( type == AFFILIATION_MESSAGE || type == AFFILIATION_ACK_MESSAGE || type == HELLO_MESSAGE || type == HELLO_ACK_MESSAGE )
    	emit( mSigHelloOverhead, s );
    else
    	emit( mSigOverhead, s );

    // fill the cluster control fields
    pkt->setNodeId( mID );

    Coord p = mMobility->getCurrentPosition();
    pkt->setXPosition( p.x );
    pkt->setYPosition( p.y );

    p = mMobility->getCurrentSpeed();
    pkt->setSpeed( p.length() );

    pkt->setXDestination( mCurrentDestination.x );
    pkt->setYDestination( mCurrentDestination.y );

    pkt->setIsClusterHead( IsClusterHead() );
    pkt->setClusterHead( mClusterHead );

    pkt->setNeighbourCount( mNeighbours.size() );
    pkt->setClusterSize( mClusterMembers.size() );

    pkt->setSrcAddr(myNetwAddr);
    pkt->setDestAddr(netwAddr);
    coreEV << " netw "<< myNetwAddr << " sending packet" <<std::endl;
    if(LAddress::isL3Broadcast( netwAddr )) {
        coreEV << "sendDown: nHop=L3BROADCAST -> message has to be broadcasted"
           << " -> set destMac=L2BROADCAST\n";
        macAddr = LAddress::L2BROADCAST;
    }
    else{
        coreEV <<"sendDown: get the MAC address\n";
        if ( simulation.getModule( static_cast<int>(netwAddr ) ) ) {
        	macAddr = arp->getMacAddr(netwAddr);
        } else {
        	coreEV << "sendDown: Cannot find a module with the network address: " << netwAddr << std::endl;
        	return;
        }
    }

    setDownControlInfo(pkt, macAddr);

    sendDown( pkt );


}






/**
 * Update neighbour data with the given message.
 */
void AmacadNetworkLayer::UpdateNeighbour( AmacadControlMessage *m ) {

	int id = m->getNodeId();
	mNeighbours[id].mID = id;
	mNeighbours[id].mNetworkAddress = m->getSrcAddr();
	mNeighbours[id].mPosition.x = m->getXPosition();
	mNeighbours[id].mPosition.y = m->getYPosition();
	mNeighbours[id].mSpeed = m->getSpeed();
	mNeighbours[id].mDestination.x = m->getXDestination();
	mNeighbours[id].mDestination.y = m->getYDestination();
	mNeighbours[id].mIsClusterHead = m->getIsClusterHead();
	mNeighbours[id].mClusterHead = m->getClusterHead();
	mNeighbours[id].mNeighbourCount = m->getNeighbourCount();
	mNeighbours[id].mClusterSize = m->getClusterSize();
	mNeighbours[id].mValueF = CalculateF(id);
	mNeighbours[id].mLastHeard = simTime();

}




/**
 * Collect CH neighbours.
 */
void AmacadNetworkLayer::CollectClusterHeadNeighbours() {

	mClusterHeadNeighbours.clear();
	for ( NeighbourIterator it = mNeighbours.begin(); it != mNeighbours.end(); it++ )
		if ( it->second.mIsClusterHead )
			mClusterHeadNeighbours.push_back( it->first );

}




/**
 * Calculate the F value.
 * If id is not -1, the Fvz value is calculated for the given ID with
 * respect to this node. If it is -1, the function sums all the Fvz values.
 * If notThis is true, then the Fv is calculated with respect to the node
 * given by id.
 */
double AmacadNetworkLayer::CalculateF( int id, bool notThis ) {

	double dL, dS, dD, ret = 0;

	if ( id != -1 ) {

		if ( notThis ) {

			// Compute the F value between all our neighbours and the given neighbour.
			for ( NeighbourIterator it = mNeighbours.begin(); it != mNeighbours.end(); it++ ) {
				if ( id == it->first )
					continue;
				dL = ( mNeighbours[id].mPosition - it->second.mPosition ).length();
				dS = fabs( mNeighbours[id].mSpeed - it->second.mSpeed );
				dD = ( mNeighbours[id].mDestination - it->second.mDestination ).length();
				ret += dL * mWeights[0] + dS * mWeights[1] + dD * mWeights[2];
			}

		} else {

			// Compute the F value between us and the given ID
			dL = ( mNeighbours[id].mPosition - mMobility->getCurrentPosition() ).length();
			dS = fabs( mNeighbours[id].mSpeed - mMobility->getCurrentSpeed().length() );
			dD = ( mNeighbours[id].mDestination - mCurrentDestination ).length();
			ret = dL * mWeights[0] + dS * mWeights[1] + dD * mWeights[2];

		}

	} else {

		// Compute our F value.
		for ( NeighbourIterator it = mNeighbours.begin(); it != mNeighbours.end(); it++ ) {
			dL = ( it->second.mPosition - mMobility->getCurrentPosition() ).length();
			dS = fabs( it->second.mSpeed - mMobility->getCurrentSpeed().length() );
			dD = ( it->second.mDestination - mCurrentDestination ).length();
			ret += dL * mWeights[0] + dS * mWeights[1] + dD * mWeights[2];
		}

	}

	return ret;

}





/**
 * Compute neighbour scores and return the ID of the best scoring one.
 */
int AmacadNetworkLayer::ComputeNeighbourScores() {

	// First calculate our score.
	double bestScore = CalculateF();
	int bestID = mID;

	// Then find a CM that beats us.
	for ( NodeIdSet::iterator it = mClusterMembers.begin(); it != mClusterMembers.end(); it++ ) {

		// Calculate this CMs score with respect to the other CMs.
		double currScore = CalculateF( *it, true );
		if ( currScore < bestScore ) {

			// We have a new best scorer.
			bestScore = currScore;
			bestID = *it;

		}

	}

	// Then find a neighbouring CH that beats us.
	for ( NodeIdList::iterator it = mClusterHeadNeighbours.begin(); it != mClusterHeadNeighbours.end(); it++ ) {

		// Calculate this CMs score with respect to the other CMs.
		double currScore = CalculateF( *it, true );
		if ( currScore < bestScore ) {

			// We have a new best scorer.
			bestScore = currScore;
			bestID = *it;

		}

	}

	return bestID;

}



/**
 * Change the CH.
 */
void AmacadNetworkLayer::ChangeCH( int id ) {

	// Look through the list of members we've collected.
	// We calculate each node's aggregate Fv score and elect the lowest score as the CH.
	int ret;

	if ( id == -1 )
		ret = id;
	else
		ret = ComputeNeighbourScores();

	// Check if we have the best score.
	if ( ret != mID ) {

		// We don't have the best score.
		// Tell the best scorer to become CH, and tell the other CMs to join it.
		// Then we become CM of the new CH.
		for ( NodeIdSet::iterator it = mClusterMembers.begin(); it != mClusterMembers.end(); it++ )
			if ( *it != ret )
				SendControlMessage( MEMBER_UPDATE_MESSAGE, *it );
		SendControlMessage( CLUSTERHEAD_ACK_MESSAGE, ret );
		ClusterDied( CD_Cannibal );
		mCurrentState = ClusterMember;
		mIsClusterHead = false;
		mClusterHead = ret;
		mClusterMembers.clear();

	} else {

		// We are the CH
		mCurrentState = ClusterHead;
		ClusterStarted();
    	//std::cerr << "Node " << mID << ": Became CH.\n";

	}

}


/**
 * Get the destination from the destination file.
 */
void AmacadNetworkLayer::GetDestinationList() {

	mDestinationSet.clear();

	std::ifstream inputStream;
	inputStream.open( par("destinationFile").stringValue() );
	if ( inputStream.fail() )
		throw cRuntimeError( "Cannot load the destination file." );


	TraCIMobility *mob = dynamic_cast<TraCIMobility*>(mMobility);
	std::string carName, myName = mob->getExternalId();
	int count;
	DestinationEntry e;
	int entryCount;
	bool save;

	inputStream >> entryCount;
	for ( int i = 0; i < entryCount; i++ ) {

		inputStream >> carName >> count;
		save = ( carName == myName );
		for ( int j = 0; j < count; j++ ) {

			inputStream >> e.mTime >> e.mDestination.x >> e.mDestination.y;
			if ( save )
				mDestinationSet.push_back( e );

		}

		if ( save )
			break;

	}

	inputStream.close();

}




