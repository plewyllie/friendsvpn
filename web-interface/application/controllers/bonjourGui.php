<?php if ( ! defined('BASEPATH')) exit('No direct script access allowed');
/**
 * This class will handle AJAX requests coming from view home.php
 */
class BonjourGui extends MY_Controller {    
    public function __construct() {
        parent::__construct();
        $this->load->model('UserSQL');
        $this->load->library('Facebook');
        $this->load->library('xmlrpc');
        $this->load->model('RpcSQL');
    }
    
    /**
     * Shows the available services in a column
     */
    public function services() {
        $data["services"] = $this->UserSQL->getServices($this->facebook->getUser());
        
        $this->load->helper('plist_helper');
        
        $mappings = new DOMDocument();
        $mappings->load(base_url(APPPATH . "/assets/Services.plist"));
        
        $parsed = parsePlist($mappings)["Services"];
        
        if ($data["services"]) {
            for ($i = 0; $i < count($data["services"]); $i++) {
                $name = $data["services"][$i]["name"];
                $data["services"][$i]["fancy_name"] = $name;
                foreach ($parsed as $mapping) {
                    if ($mapping["Service"] === $name) {
                        $data["services"][$i]["fancy_name"] = $mapping["Name"];
                    }
                }
            }
        }

        
        $this->load->view("bonjourGuiComponents/services.php", $data);
    }
    
    /**
     * This function will render a page with a list of the hostnames
     * and links to a page with a list of the services for that host
     */
    public function hostnames() {
        // get post data
        $service = $this->input->post("service");
        
        $data["hostnames"] = $this->UserSQL->getHostNames($this->facebook->getUser(), $service);
        $this->load->view("bonjourGuiComponents/hostnames.php", $data);
    }

    /**
     * Shows the available services and names for the given service and hostname passed through POST data
     */
    public function serviceNamePortList() {
        // get post data
        $service = $this->input->post("service");
        $hostname = $this->input->post("hostname");
        
        $data["namePorts"] = $this->UserSQL->getNamesAndPort($this->facebook->getUser(), $hostname, $service);
        $this->load->view("bonjourGuiComponents/namePortList.php", $data);
    }
    
    /**
     * Renders the "details" view for a given service
     */
    public function details() {
        // get post data
        $service = $this->input->post("service");
        $hostname = $this->input->post("hostname");
        $name = $this->input->post("name");
        $port = $this->input->post("port");
        $transProt = $this->input->post("transProt");
        // get facebook friends and permissions
        $this->load->model("FacebookFQL");
        $appFriends = [];
        $appFriendsDenied = [];
        $friends = $this->FacebookFQL->getFriends();
        $i = $j = 0;
        foreach ($friends as $friend) {
            if ($this->UserSQL->friendInDb($friend["uid"])) {
                $friend["authorized"] = $this->UserSQL->friendAuthorized($this->facebook->getUser(), $friend["uid"], $service, $hostname, $name, $port, $transProt);
                if ($friend["authorized"]) {
                    $appFriends[$i] = $friend;
                    $i++;
                } else {
                    $appFriendsDenied[$j] = $friend;
                    $j++;
                }
            }
        }
        
        $data["friends_auth"] = $appFriends;
        $data["friends_denied"] = $appFriendsDenied;
        $this->load->view("bonjourGuiComponents/details.php", $data);
    }
    
    public function deAuthorizeUser() {
        // get post data
        $service = $this->input->post("service");
        $hostname = $this->input->post("hostname");
        $name = $this->input->post("name");
        $port = $this->input->post("port");
        $transProt = $this->input->post("transProt");
        $uid = $this->input->post("uid");
        $this->UserSQL->deAuthorizeUser($this->facebook->getUser(), $uid, $service, $hostname, $name, $port, $transProt);
        
        /* add xmlrpc notif */
        $request = array("test");
        $this->xmlrpc->request($request);
		$this->xmlrpc->method('emitBonjourChanged');
        $requestRPCString = $this->xmlrpc->get_request();
        $requestId = $this->RpcSQL->addRequest($_SERVER["REMOTE_ADDR"], $requestRPCString);
    }
    
    public function authorizeUser() {
        // get post data
        $service = $this->input->post("service");
        $hostname = $this->input->post("hostname");
        $name = $this->input->post("name");
        $port = $this->input->post("port");
        $transProt = $this->input->post("transProt");
        $uid = $this->input->post("uid");
        $this->UserSQL->authorizeUser($this->facebook->getUser(), $uid, $service, $hostname, $name, $port, $transProt);
        
        /* add xmlrpc notif */
        $request = array("test");
        $this->xmlrpc->request($request);
		$this->xmlrpc->method('emitBonjourChanged');
        $requestRPCString = $this->xmlrpc->get_request();
        $requestId = $this->RpcSQL->addRequest($_SERVER["REMOTE_ADDR"], $requestRPCString);
    }
    
    public function deleteRecord() {
                // get post data
        $service = $this->input->post("service");
        $hostname = $this->input->post("hostname");
        $name = $this->input->post("name");
        $port = $this->input->post("port");
        $transProt = $this->input->post("transProt");
        $uid = $this->input->post("uid");
        
        $this->UserSQL->removeRecord($this->facebook->getUser(), $uid, $service, $hostname, $name, $port, $transProt);
        
        /* add xmlrpc notif */
        $request = array("test");
        $this->xmlrpc->request($request);
		$this->xmlrpc->method('emitBonjourChanged');
        $requestRPCString = $this->xmlrpc->get_request();
        $requestId = $this->RpcSQL->addRequest($_SERVER["REMOTE_ADDR"], $requestRPCString);
    }
    
    public function deleteAll() {
        $this->UserSQL->removeAllRecords($this->facebook->getUser());
    }
}

/* End of file welcome.php */
/* Location: ./application/controllers/welcome.php */