/* eslint-disable no-undef */
/* eslint-disable @typescript-eslint/no-var-requires */
/*---------------------------------------------------------------------------------------------
 *  Copyright (c) Microsoft Corporation. All rights reserved.
 *  Licensed under the MIT License. See License.txt in the project root for license information.
 *--------------------------------------------------------------------------------------------*/

//@ts-check
'use strict';

//@ts-check
/** @typedef {import('webpack').Configuration} WebpackConfig **/

const path = require('path');
const webpack = require('webpack');

/** @type WebpackConfig */
const webClientConfig = {
	context: path.join(__dirname, 'lsp-client'),
	mode: 'none',
	target: 'webworker', // web extensions run in a webworker context
	entry: {
		clientWeb: './src/clientWeb.ts',
	},
	output: {
		filename: '[name].js',
		path: path.join(__dirname, 'lsp-client', 'out'),
		libraryTarget: 'commonjs',
	},
	resolve: {
		mainFields: ['module', 'main'],
		extensions: ['.ts', '.js'], // support ts-files and js-files
		alias: {},
		fallback: {
			path: require.resolve('path-browserify')
		},
	},
	module: {
		rules: [
			{
				test: /\.ts$/,
				exclude: /node_modules/,
				use: [
					{
						loader: 'ts-loader',
					},
				],
			},
		],
	},
	externals: {
		vscode: 'commonjs vscode', // ignored because it doesn't exist
	},
	performance: {
		hints: false,
	},
	devtool: 'source-map',
};

/** @type WebpackConfig */
const webServerConfig = {
	context: path.join(__dirname, 'lsp-server'),
	mode: 'none',
	target: 'webworker',
	entry: {
		serverWeb: './src/serverWeb.ts',
	},
	output: {
		filename: '[name].js',
		path: path.join(__dirname, 'lsp-server', 'out'),
		libraryTarget: 'var',
		library: 'serverExportVar',
	},
	resolve: {
		mainFields: ['module', 'main'],
		extensions: ['.ts', '.js'],
		alias: {},
		fallback: {
			path: require.resolve('path-browserify'),
			crypto: require.resolve('crypto-browserify'),
			util: require.resolve('util/'),
			fs: false,
		}
	},
	module: {
		rules: [
			{
				test: /\.ts$/,
				exclude: /node_modules/,
				use: [
					{
						loader: 'ts-loader',
					},
				],
			},
		],
	},
	plugins: [
		new webpack.ProvidePlugin({
			process: 'process/browser' // provide a shim for the global `process` variable
		})
	],
	externals: {
		vscode: 'commonjs vscode',
	},
	performance: {
		hints: false,
	},
	devtool: 'source-map',
};

/** @type WebpackConfig */
const nodeClientConfig = {
	mode: 'none', // this leaves the source code as close as possible to the original
				  // (when packaging we set this to 'production')
	target: 'node', // node extensions run in a node context
	entry: {
		clientNode: './lsp-client/src/clientNode.ts'
	},
	output: {
		filename: '[name].js',
		path: path.join(__dirname, './lsp-client/out'),
		libraryTarget: 'commonjs',
		devtoolModuleFilenameTemplate: '../../[resource-path]'
	},
	resolve: {
		mainFields: ['module', 'main'],
		extensions: ['.ts', '.js'],
	},
	module: {
		rules: [{
			test: /\.ts$/,
			exclude: /node_modules/,
			use: [{
				loader: 'ts-loader'
			}]
		}]
	},
	externals: {
		'vscode': 'commonjs vscode',
	},
	performance: {
		hints: false
	},
	devtool: 'nosources-source-map', // create a source map that points to the original source file
	infrastructureLogging: {
		level: "log", // enables logging required for problem matchers
	},
};

/** @type WebpackConfig */
const nodeServerConfig = {
	context: path.join(__dirname, 'lsp-server'),
	mode: 'none',
	target: 'node',
	entry: {
		serverNode: './src/serverNode.ts',
	},
	output: {
		filename: '[name].js',
		path: path.join(__dirname, 'lsp-server', 'out'),
		libraryTarget: 'var',
		library: 'serverExportVar',
	},
	resolve: {
		mainFields: ['module', 'main'],
		extensions: ['.ts', '.js'], // support ts-files and js-files
	},
	module: {
		rules: [
			{
				test: /\.ts$/,
				exclude: /node_modules/,
				use: [
					{
						loader: 'ts-loader',
					},
				],
			},
		],
	},
	externals: {
		vscode: 'commonjs vscode',
	},
	performance: {
		hints: false,
	},
	devtool: 'source-map',
};

module.exports = [webClientConfig, webServerConfig, nodeClientConfig, nodeServerConfig];
