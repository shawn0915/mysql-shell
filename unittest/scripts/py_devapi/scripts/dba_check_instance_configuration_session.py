# Assumptions: smart deployment routines available

#@ Check Instance Configuration must work without a session
dba.check_instance_configuration({'host': localhost, 'port': __mysql_sandbox_port1, 'password': 'root'})

#@ First Sandbox
deployed_here = reset_or_deploy_sandbox(__mysql_sandbox_port1)

#@ Check Instance Configuration ok with a session
dba.check_instance_configuration({'host': localhost, 'port': __mysql_sandbox_port1, 'password': 'root'})

# Remove the sandbox
if deployed_here:
	cleanup_sandbox(__mysql_sandbox_port1);