/*
 * ZeroTier SDK - Network Virtualization Everywhere
 * Copyright (C) 2011-2017  ZeroTier, Inc.  https://www.zerotier.com/
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * --
 *
 * You can be released from the requirements of the license by purchasing
 * a commercial license. Buying such a license is mandatory as soon as you
 * develop commercial closed-source software that incorporates or links
 * directly against ZeroTier software without disclosing the source code
 * of your own application.
 */

/**
 * @file
 *
 * ZeroTier One service control wrapper
 */

#include "ZT1Service.h"

#include "Debug.hpp"

#include "Phy.hpp"
#include "OneService.hpp"
#include "Utilities.h"
#include "OSUtils.hpp"

#ifdef __cplusplus
extern "C" {
#endif

namespace ZeroTier {
	std::vector<void*> vtaps;

	static ZeroTier::OneService *zt1Service;

	std::string homeDir; // Platform-specific dir we *must* use internally
	std::string netDir;  // Where network .conf files are to be written

	ZeroTier::Mutex _vtaps_lock;
	ZeroTier::Mutex _multiplexer_lock;
}

/****************************************************************************/
/* ZeroTier Core helper functions for libzt - DON'T CALL THESE DIRECTLY     */
/****************************************************************************/

std::vector<ZT_VirtualNetworkRoute> *zts_get_network_routes(char *nwid)
{
	uint64_t nwid_int = strtoull(nwid, NULL, 16);
	return ZeroTier::zt1Service->getRoutes(nwid_int);
}

ZeroTier::VirtualTap *getTapByNWID(uint64_t nwid)
{
	ZeroTier::_vtaps_lock.lock();
	ZeroTier::VirtualTap *s, *tap = nullptr;
	for (int i=0; i<ZeroTier::vtaps.size(); i++) {
		s = (ZeroTier::VirtualTap*)ZeroTier::vtaps[i];
		if (s->_nwid == nwid) { tap = s; }
	}
	ZeroTier::_vtaps_lock.unlock();
	return tap;
}

ZeroTier::VirtualTap *getTapByAddr(ZeroTier::InetAddress *addr)
{
	ZeroTier::_vtaps_lock.lock();
	ZeroTier::VirtualTap *s, *tap = nullptr;
	//char ipbuf[64], ipbuf2[64], ipbuf3[64];
	for (int i=0; i<ZeroTier::vtaps.size(); i++) {
		s = (ZeroTier::VirtualTap*)ZeroTier::vtaps[i];
		// check address schemes
		for (int j=0; j<s->_ips.size(); j++) {
			if ((s->_ips[j].isV4() && addr->isV4()) || (s->_ips[j].isV6() && addr->isV6())) {
				//DEBUG_EXTRA("looking at tap %s, <addr=%s> --- for <%s>", s->_dev.c_str(), s->_ips[j].toString(ipbuf), addr->toIpString(ipbuf2));
				if (s->_ips[j].isEqualPrefix(addr)
					|| s->_ips[j].ipsEqual(addr)
					|| s->_ips[j].containsAddress(addr)
					|| (addr->isV6() && ipv6_in_subnet(&s->_ips[j], addr))
					)
				{
					//DEBUG_EXTRA("selected tap %s, <addr=%s>", s->_dev.c_str(), s->_ips[j].toString(ipbuf));
					ZeroTier::_vtaps_lock.unlock();
					return s;
				}
			}
		}
		// check managed routes
		if (tap == NULL) {
			std::vector<ZT_VirtualNetworkRoute> *managed_routes = ZeroTier::zt1Service->getRoutes(s->_nwid);
			ZeroTier::InetAddress target, nm, via;
			for (int i=0; i<managed_routes->size(); i++) {
				target = managed_routes->at(i).target;
				nm = target.netmask();
				via = managed_routes->at(i).via;
				if (target.containsAddress(addr)) {
					//DEBUG_EXTRA("chose tap with route <target=%s, nm=%s, via=%s>", target.toString(ipbuf), nm.toString(ipbuf2), via.toString(ipbuf3));
					ZeroTier::_vtaps_lock.unlock();
					return s;
				}
			}
		}
	}
	ZeroTier::_vtaps_lock.unlock();
	return tap;
}

ZeroTier::VirtualTap *getTapByName(char *ifname)
{
	ZeroTier::_vtaps_lock.lock();
	ZeroTier::VirtualTap *s, *tap = nullptr;
	for (int i=0; i<ZeroTier::vtaps.size(); i++) {
		s = (ZeroTier::VirtualTap*)ZeroTier::vtaps[i];
		if (strcmp(s->_dev.c_str(), ifname) == false) {
			tap = s;
		}
	}
	ZeroTier::_vtaps_lock.unlock();
	return tap;
}

ZeroTier::VirtualTap *getTapByIndex(int index)
{
	ZeroTier::_vtaps_lock.lock();
	ZeroTier::VirtualTap *s, *tap = nullptr;
	for (int i=0; i<ZeroTier::vtaps.size(); i++) {
		s = (ZeroTier::VirtualTap*)ZeroTier::vtaps[i];
		if (s->ifindex == index) {
			tap = s;
		}
	}
	ZeroTier::_vtaps_lock.unlock();
	return tap;
}

ZeroTier::VirtualTap *getAnyTap()
{
	ZeroTier::_vtaps_lock.lock();
	ZeroTier::VirtualTap *vtap = NULL;
	if (ZeroTier::vtaps.size()) {
		vtap = (ZeroTier::VirtualTap *)ZeroTier::vtaps[0];
	}
	ZeroTier::_vtaps_lock.unlock();
	return vtap;
}

int zts_get_device_id_from_file(const char *filepath, char *devID) {
	std::string fname("identity.public");
	std::string fpath(filepath);
	if (ZeroTier::OSUtils::fileExists((fpath + ZT_PATH_SEPARATOR_S + fname).c_str(),false)) {
		std::string oldid;
		ZeroTier::OSUtils::readFile((fpath + ZT_PATH_SEPARATOR_S + fname).c_str(),oldid);
		memcpy(devID, oldid.c_str(), 10); // first 10 bytes of file
		return 0;
	}
	return -1;
}

// Starts a ZeroTier service in the background
void *zts_start_service(void *thread_id)
{
	DEBUG_INFO("homeDir=%s", ZeroTier::homeDir.c_str());
	// Where network .conf files will be stored
	ZeroTier::netDir = ZeroTier::homeDir + "/networks.d";
	ZeroTier::zt1Service = (ZeroTier::OneService *)0;
	// Construct path for network config and supporting service files
	if (ZeroTier::homeDir.length()) {
		std::vector<std::string> hpsp(ZeroTier::OSUtils::split(ZeroTier::homeDir.c_str(), ZT_PATH_SEPARATOR_S,"",""));
		std::string ptmp;
		if (ZeroTier::homeDir[0] == ZT_PATH_SEPARATOR) {
			ptmp.push_back(ZT_PATH_SEPARATOR);
		}
		for (std::vector<std::string>::iterator pi(hpsp.begin());pi!=hpsp.end();++pi) {
			if (ptmp.length() > 0) {
				ptmp.push_back(ZT_PATH_SEPARATOR);
			}
			ptmp.append(*pi);
			if ((*pi != ".")&&(*pi != "..")) {
				if (ZeroTier::OSUtils::mkdir(ptmp) == false) {
					DEBUG_ERROR("home path does not exist, and could not create");
					handle_general_failure();
					perror("error\n");
				}
			}
		}
	}
	else {
		DEBUG_ERROR("homeDir is empty, could not construct path");
		handle_general_failure();
		return NULL;
	}

	// Generate random port for new service instance
	unsigned int randp = 0;
	ZeroTier::Utils::getSecureRandom(&randp,sizeof(randp));
	// TODO: Better port random range selection
	int servicePort = 9000 + (randp % 1000);
	for (;;) {
		ZeroTier::zt1Service = ZeroTier::OneService::newInstance(ZeroTier::homeDir.c_str(),servicePort);
		switch(ZeroTier::zt1Service->run()) {
			case ZeroTier::OneService::ONE_STILL_RUNNING:
			case ZeroTier::OneService::ONE_NORMAL_TERMINATION:
				break;
			case ZeroTier::OneService::ONE_UNRECOVERABLE_ERROR:
				DEBUG_ERROR("ZTO service port = %d", servicePort);
				DEBUG_ERROR("fatal error: %s",ZeroTier::zt1Service->fatalErrorMessage().c_str());
				break;
			case ZeroTier::OneService::ONE_IDENTITY_COLLISION: {
				delete ZeroTier::zt1Service;
				ZeroTier::zt1Service = (ZeroTier::OneService *)0;
				std::string oldid;
				ZeroTier::OSUtils::readFile((ZeroTier::homeDir + ZT_PATH_SEPARATOR_S
					+ "identity.secret").c_str(),oldid);
				if (oldid.length()) {
					ZeroTier::OSUtils::writeFile((ZeroTier::homeDir + ZT_PATH_SEPARATOR_S
						+ "identity.secret.saved_after_collision").c_str(),oldid);
					ZeroTier::OSUtils::rm((ZeroTier::homeDir + ZT_PATH_SEPARATOR_S
						+ "identity.secret").c_str());
					ZeroTier::OSUtils::rm((ZeroTier::homeDir + ZT_PATH_SEPARATOR_S
						+ "identity.public").c_str());
				}
			}
			continue; // restart!
		}
		break; // terminate loop -- normally we don't keep restarting
	}
	delete ZeroTier::zt1Service;
	ZeroTier::zt1Service = (ZeroTier::OneService *)0;
	return NULL;
}

void disableTaps()
{
	ZeroTier::_vtaps_lock.lock();
	for (int i=0; i<ZeroTier::vtaps.size(); i++) {
		DEBUG_EXTRA("vt=%p", ZeroTier::vtaps[i]);
		((ZeroTier::VirtualTap*)ZeroTier::vtaps[i])->_enabled = false;
	}
	ZeroTier::_vtaps_lock.unlock();
}

void zts_get_ipv4_address(const char *nwid, char *addrstr, const int addrlen)
{
	if (ZeroTier::zt1Service) {
		uint64_t nwid_int = strtoull(nwid, NULL, 16);
		ZeroTier::VirtualTap *tap = getTapByNWID(nwid_int);
		if (tap && tap->_ips.size()) {
			for (int i=0; i<tap->_ips.size(); i++) {
				if (tap->_ips[i].isV4()) {
					char ipbuf[INET_ADDRSTRLEN];
					std::string addr = tap->_ips[i].toString(ipbuf);
					int len = addrlen < addr.length() ? addrlen : addr.length();
					memset(addrstr, 0, len);
					memcpy(addrstr, addr.c_str(), len);
					return;
				}
			}
		}
	}
	else
		memcpy(addrstr, "\0", 1);
}

void zts_get_ipv6_address(const char *nwid, char *addrstr, const int addrlen)
{
	if (ZeroTier::zt1Service) {
		uint64_t nwid_int = strtoull(nwid, NULL, 16);
		ZeroTier::VirtualTap *tap = getTapByNWID(nwid_int);
		if (tap && tap->_ips.size()) {
			for (int i=0; i<tap->_ips.size(); i++) {
				if (tap->_ips[i].isV6()) {
					char ipbuf[INET6_ADDRSTRLEN];
					std::string addr = tap->_ips[i].toString(ipbuf);
					int len = addrlen < addr.length() ? addrlen : addr.length();
					memset(addrstr, 0, len);
					memcpy(addrstr, addr.c_str(), len);
					return;
				}
			}
		}
	}
	else
		memcpy(addrstr, "\0", 1);
}

int zts_has_ipv4_address(const char *nwid)
{
	char ipv4_addr[INET_ADDRSTRLEN];
	memset(ipv4_addr, 0, INET_ADDRSTRLEN);
	zts_get_ipv4_address(nwid, ipv4_addr, INET_ADDRSTRLEN);
	return strcmp(ipv4_addr, "\0");
}

int zts_has_ipv6_address(const char *nwid)
{
	char ipv6_addr[INET6_ADDRSTRLEN];
	memset(ipv6_addr, 0, INET6_ADDRSTRLEN);
	zts_get_ipv6_address(nwid, ipv6_addr, INET6_ADDRSTRLEN);
	return strcmp(ipv6_addr, "\0");
}

int zts_has_address(const char *nwid)
{
	return zts_has_ipv4_address(nwid) || zts_has_ipv6_address(nwid);
}


void zts_get_6plane_addr(char *addr, const char *nwid, const char *devID)
{
	ZeroTier::InetAddress _6planeAddr = ZeroTier::InetAddress::makeIpv66plane(
		ZeroTier::Utils::hexStrToU64(nwid),ZeroTier::Utils::hexStrToU64(devID));
	char ipbuf[INET6_ADDRSTRLEN];
	memcpy(addr, _6planeAddr.toIpString(ipbuf), 40);
}

void zts_get_rfc4193_addr(char *addr, const char *nwid, const char *devID)
{
	ZeroTier::InetAddress _6planeAddr = ZeroTier::InetAddress::makeIpv6rfc4193(
		ZeroTier::Utils::hexStrToU64(nwid),ZeroTier::Utils::hexStrToU64(devID));
	char ipbuf[INET6_ADDRSTRLEN];
	memcpy(addr, _6planeAddr.toIpString(ipbuf), 40);
}

void zts_join(const char * nwid) {
	if (ZeroTier::zt1Service) {
		std::string confFile = ZeroTier::zt1Service->givenHomePath() + "/networks.d/" + nwid + ".conf";
		if (ZeroTier::OSUtils::mkdir(ZeroTier::netDir) == false) {
			DEBUG_ERROR("unable to create: %s", ZeroTier::netDir.c_str());
			handle_general_failure();
		}
		if (ZeroTier::OSUtils::writeFile(confFile.c_str(), "") == false) {
			DEBUG_ERROR("unable to write network conf file: %s", confFile.c_str());
			handle_general_failure();
		}
		ZeroTier::zt1Service->join(nwid);
	}
	// provide ZTO service reference to virtual taps
	// TODO: This might prove to be unreliable, but it works for now
	for (int i=0;i<ZeroTier::vtaps.size(); i++) {
		ZeroTier::VirtualTap *s = (ZeroTier::VirtualTap*)ZeroTier::vtaps[i];
		s->zt1ServiceRef=(void*)ZeroTier::zt1Service;
	}
}

void zts_join_soft(const char * filepath, const char * nwid) {
	std::string net_dir = std::string(filepath) + "/networks.d/";
	std::string confFile = net_dir + std::string(nwid) + ".conf";
	if (ZeroTier::OSUtils::mkdir(net_dir) == false) {
		DEBUG_ERROR("unable to create: %s", net_dir.c_str());
		handle_general_failure();
	}
	if (ZeroTier::OSUtils::fileExists(confFile.c_str(), false) == false) {
		if (ZeroTier::OSUtils::writeFile(confFile.c_str(), "") == false) {
			DEBUG_ERROR("unable to write network conf file: %s", confFile.c_str());
			handle_general_failure();
		}
	}
}

void zts_leave(const char * nwid) {
	if (ZeroTier::zt1Service) {
		ZeroTier::zt1Service->leave(nwid);
	}
}

void zts_leave_soft(const char * filepath, const char * nwid) {
	std::string net_dir = std::string(filepath) + "/networks.d/";
	ZeroTier::OSUtils::rm((net_dir + nwid + ".conf").c_str());
}

int zts_running() {
	return ZeroTier::zt1Service == NULL ? false : ZeroTier::zt1Service->isRunning();
}

void zts_start(const char *path)
{
	if (ZeroTier::zt1Service) {
		return;
	}
	if (path) {
		ZeroTier::homeDir = path;
	}
	pthread_t service_thread;
	pthread_create(&service_thread, NULL, zts_start_service, NULL);
}

void zts_simple_start(const char *path, const char *nwid)
{
	zts_start(path);
	while (zts_running() == false) {
		DEBUG_EXTRA("waiting for service to start");
		nanosleep((const struct timespec[]) {{0, (ZTO_WRAPPER_CHECK_INTERVAL * 1000000)}}, NULL);
	}
	while (true) {
		DEBUG_EXTRA("trying join");
		try {
			zts_join(nwid);
			DEBUG_EXTRA("joined");
			break;
		}
		catch( ... ) {
			DEBUG_ERROR("there was a problem joining the virtual network");
			handle_general_failure();
		}
	}
	DEBUG_EXTRA("waiting for addresss");
	while (zts_has_address(nwid) == false) {
		nanosleep((const struct timespec[]) {{0, (ZTO_WRAPPER_CHECK_INTERVAL * 1000000)}}, NULL);
	}
}

void zts_stop() {
	if (ZeroTier::zt1Service) {
		ZeroTier::zt1Service->terminate();
		disableTaps();
	}
}

void zts_get_homepath(char *homePath, int len) {
	if (ZeroTier::homeDir.length()) {
		memset(homePath, 0, len);
		int buf_len = len < ZeroTier::homeDir.length() ? len : ZeroTier::homeDir.length();
		memcpy(homePath, ZeroTier::homeDir.c_str(), buf_len);
	}
}

int zts_get_device_id(char *devID) {
	if (ZeroTier::zt1Service) {
		char id[ZTO_ID_LEN];
		sprintf(id, "%lx",ZeroTier::zt1Service->getNode()->address());
		memcpy(devID, id, ZTO_ID_LEN);
		return 0;
	}
	else // Service isn't online, try to read ID from file
	{
		std::string fname("identity.public");
		std::string fpath(ZeroTier::homeDir);
		if (ZeroTier::OSUtils::fileExists((fpath + ZT_PATH_SEPARATOR_S + fname).c_str(),false)) {
			std::string oldid;
			ZeroTier::OSUtils::readFile((fpath + ZT_PATH_SEPARATOR_S + fname).c_str(),oldid);
			memcpy(devID, oldid.c_str(), ZTO_ID_LEN); // first 10 bytes of file
			return 0;
		}
	}
	return -1;
}

unsigned long zts_get_peer_count() {
	if (ZeroTier::zt1Service) {
		return ZeroTier::zt1Service->getNode()->peers()->peerCount;
	}
	else {
		return 0;
	}
}

int zts_get_peer_address(char *peer, const char *devID) {
	if (ZeroTier::zt1Service) {
		ZT_PeerList *pl = ZeroTier::zt1Service->getNode()->peers();
		// uint64_t addr;
		for (int i=0; i<pl->peerCount; i++) {
			// ZT_Peer *p = &(pl->peers[i]);
			// DEBUG_INFO("peer[%d] = %lx", i, p->address);
		}
		return pl->peerCount;
	}
	else
		return -1;
}

void zts_allow_http_control(bool allowed)
{
	// TODO
}

#ifdef __cplusplus
}
#endif
